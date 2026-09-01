#include "sierrachart.h"

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstring>

SCDLLName("Full LOB Change - Raw Depth Events")

namespace
{
    static const uint32_t DEPTH_FILE_ID = 0x44444353; // "SCDD"
    static const uint8_t CMD_CLEAR_BOOK = 1;
    static const uint8_t CMD_ADD_BID = 2;
    static const uint8_t CMD_ADD_ASK = 3;
    static const uint8_t CMD_MODIFY_BID = 4;
    static const uint8_t CMD_MODIFY_ASK = 5;
    static const uint8_t CMD_DELETE_BID = 6;
    static const uint8_t CMD_DELETE_ASK = 7;
    static const uint8_t FLAG_END_OF_BATCH = 0x01;

#pragma pack(push, 1)
    struct DepthFileHeaderRaw
    {
        uint32_t FileTypeUniqueHeaderID;
        uint32_t HeaderSize;
        uint32_t RecordSize;
        uint32_t Version;
        char Reserve[48];
    };

    struct DepthRecordRaw
    {
        int64_t DateTimeUTC;
        uint8_t Command;
        uint8_t Flags;
        uint16_t NumOrders;
        float Price;
        uint32_t Quantity;
        uint32_t Reserved;
    };
#pragma pack(pop)

    static_assert(sizeof(DepthFileHeaderRaw) == 64, "Unexpected depth header size");
    static_assert(sizeof(DepthRecordRaw) == 24, "Unexpected depth record size");

    inline uint64_t AbsDiffU32(const uint32_t A, const uint32_t B)
    {
        return A >= B ? static_cast<uint64_t>(A - B) : static_cast<uint64_t>(B - A);
    }

    struct EngineState
    {
        HANDLE FileHandle = INVALID_HANDLE_VALUE;
        DepthFileHeaderRaw Header = {};
        std::string OpenPath;
        std::string OpenSymbol;
        int OpenUTCDateKey = 0;

        std::vector<char> ReadBuffer;
        size_t BufferPos = 0;
        size_t BufferSize = 0;

        std::unordered_map<int64_t, uint32_t> BidBook;
        std::unordered_map<int64_t, uint32_t> AskBook;
        bool HaveValidBook = false;
        bool InSnapshot = false;

        int64_t LastTargetUTC = 0;
        int64_t LastProcessedRecordUTC = 0;

        // Monotonic record->bar mapper. Depth records are chronological, so
        // there is no reason to binary-search the chart for every event.
        int MapBarIndex = -1;
        int64_t NextBarStartUTC = INT64_MAX;
        int64_t LastMappedRecordUTC = 0;

        bool LoggedOpenError = false;
        bool LoggedFormatError = false;

        EngineState()
        {
            // 1 MiB: ~43,690 raw 24-byte depth records per disk read.
            ReadBuffer.resize(1024 * 1024);
            BidBook.reserve(4096);
            AskBook.reserve(4096);
        }
    };

    void CloseDepthFile(EngineState& S)
    {
        if (S.FileHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(S.FileHandle);
            S.FileHandle = INVALID_HANDLE_VALUE;
        }

        S.OpenPath.clear();
        S.OpenSymbol.clear();
        S.OpenUTCDateKey = 0;
        S.BufferPos = 0;
        S.BufferSize = 0;
        std::memset(&S.Header, 0, sizeof(S.Header));
    }

    bool SeekAbsolute(HANDLE FileHandle, const uint64_t Offset)
    {
        LARGE_INTEGER Position;
        Position.QuadPart = static_cast<LONGLONG>(Offset);
        return SetFilePointerEx(FileHandle, Position, NULL, FILE_BEGIN) != 0;
    }

    bool GetFileSizeBytes(HANDLE FileHandle, uint64_t& SizeOut)
    {
        LARGE_INTEGER Size;
        if (!GetFileSizeEx(FileHandle, &Size))
            return false;

        SizeOut = static_cast<uint64_t>(Size.QuadPart);
        return true;
    }

    bool ReadExact(HANDLE FileHandle, void* Destination, const DWORD Bytes)
    {
        DWORD BytesRead = 0;
        if (!ReadFile(FileHandle, Destination, Bytes, &BytesRead, NULL))
            return false;

        return BytesRead == Bytes;
    }

    bool ReadRecordAt(
        HANDLE FileHandle,
        const DepthFileHeaderRaw& Header,
        const uint64_t RecordIndex,
        DepthRecordRaw& Record)
    {
        const uint64_t Offset = static_cast<uint64_t>(Header.HeaderSize)
            + RecordIndex * static_cast<uint64_t>(Header.RecordSize);

        if (!SeekAbsolute(FileHandle, Offset))
            return false;

        return ReadExact(FileHandle, &Record, sizeof(Record));
    }

    uint64_t GetCompleteRecordCount(HANDLE FileHandle, const DepthFileHeaderRaw& Header)
    {
        uint64_t FileSize = 0;
        if (!GetFileSizeBytes(FileHandle, FileSize))
            return 0;

        if (FileSize <= Header.HeaderSize || Header.RecordSize == 0)
            return 0;

        return (FileSize - Header.HeaderSize) / Header.RecordSize;
    }

    // Fixed-size records let us binary-search by the actual depth timestamp.
    // Returns the index of the last complete record <= TargetUTC.
    // Returns UINT64_MAX when every record is later than TargetUTC.
    uint64_t FindLastRecordAtOrBefore(
        HANDLE FileHandle,
        const DepthFileHeaderRaw& Header,
        const int64_t TargetUTC)
    {
        const uint64_t Count = GetCompleteRecordCount(FileHandle, Header);
        if (Count == 0)
            return UINT64_MAX;

        uint64_t Low = 0;
        uint64_t High = Count;

        while (Low < High)
        {
            const uint64_t Mid = Low + (High - Low) / 2;
            DepthRecordRaw Record = {};
            if (!ReadRecordAt(FileHandle, Header, Mid, Record))
                return UINT64_MAX;

            if (Record.DateTimeUTC <= TargetUTC)
                Low = Mid + 1;
            else
                High = Mid;
        }

        if (Low == 0)
            return UINT64_MAX;

        return Low - 1;
    }

    // Sierra writes a full LOB snapshot every 10 minutes. Find the most recent
    // CLEAR_BOOK at/before the target and restart there. This avoids rereading
    // an entire day's potentially massive .depth file when a replay starts,
    // rewinds, or the study is added mid-session.
    uint64_t FindPreviousSnapshotStart(
        HANDLE FileHandle,
        const DepthFileHeaderRaw& Header,
        const uint64_t LastRecordIndex)
    {
        if (LastRecordIndex == UINT64_MAX)
            return 0;

        const uint64_t RecordsPerBlock = 32768; // 768 KiB per reverse scan block
        std::vector<DepthRecordRaw> Block;
        Block.resize(static_cast<size_t>(RecordsPerBlock));

        uint64_t EndExclusive = LastRecordIndex + 1;

        while (EndExclusive > 0)
        {
            const uint64_t Start = EndExclusive > RecordsPerBlock
                ? EndExclusive - RecordsPerBlock
                : 0;
            const uint64_t Count = EndExclusive - Start;
            const uint64_t Offset = static_cast<uint64_t>(Header.HeaderSize)
                + Start * static_cast<uint64_t>(Header.RecordSize);

            if (!SeekAbsolute(FileHandle, Offset))
                return 0;

            const DWORD BytesWanted = static_cast<DWORD>(Count * sizeof(DepthRecordRaw));
            DWORD BytesRead = 0;
            if (!ReadFile(FileHandle, Block.data(), BytesWanted, &BytesRead, NULL))
                return 0;

            const uint64_t RecordsRead = BytesRead / sizeof(DepthRecordRaw);
            for (uint64_t I = RecordsRead; I > 0; --I)
            {
                if (Block[static_cast<size_t>(I - 1)].Command == CMD_CLEAR_BOOK)
                    return Start + I - 1;
            }

            EndExclusive = Start;
        }

        return 0;
    }

    bool OpenAndValidateDepthFile(
        SCStudyInterfaceRef sc,
        EngineState& S,
        const std::string& Path,
        const std::string& Symbol,
        const int UTCDateKey)
    {
        HANDLE Handle = CreateFileA(
            Path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            NULL);

        if (Handle == INVALID_HANDLE_VALUE)
        {
            if (!S.LoggedOpenError)
            {
                SCString Message;
                Message.Format("Full LOB Raw Events: unable to open depth file: %s", Path.c_str());
                sc.AddMessageToLog(Message, 0);
                S.LoggedOpenError = true;
            }
            return false;
        }

        DepthFileHeaderRaw Header = {};
        if (!ReadExact(Handle, &Header, sizeof(Header))
            || Header.FileTypeUniqueHeaderID != DEPTH_FILE_ID
            || Header.HeaderSize < sizeof(DepthFileHeaderRaw)
            || Header.RecordSize != sizeof(DepthRecordRaw))
        {
            CloseHandle(Handle);

            if (!S.LoggedFormatError)
            {
                SCString Message;
                Message.Format("Full LOB Raw Events: unsupported/invalid .depth file format: %s", Path.c_str());
                sc.AddMessageToLog(Message, 1);
                S.LoggedFormatError = true;
            }
            return false;
        }

        CloseDepthFile(S);
        S.FileHandle = Handle;
        S.Header = Header;
        S.OpenPath = Path;
        S.OpenSymbol = Symbol;
        S.OpenUTCDateKey = UTCDateKey;
        S.BufferPos = 0;
        S.BufferSize = 0;
        S.LoggedOpenError = false;
        S.LoggedFormatError = false;
        return true;
    }

    std::string BuildDepthFilePath(
        SCStudyInterfaceRef sc,
        const std::string& Symbol,
        const int Year,
        const int Month,
        const int Day)
    {
        SCString DataFolder = sc.DataFilesFolder();
        SCString Path;
        Path.Format(
            "%s\\MarketDepthData\\%s.%04d-%02d-%02d.depth",
            DataFolder.GetChars(),
            Symbol.c_str(),
            Year,
            Month,
            Day);
        return std::string(Path.GetChars());
    }

    std::string ResolveDepthSymbolForTime(
        SCStudyInterfaceRef sc,
        const SCDateTime& ChartDateTime)
    {
        SCString Symbol = sc.Symbol;

        const int BarIndex = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, ChartDateTime);
        if (BarIndex >= 0 && sc.ContinuousFuturesContractOption != CFCO_NONE)
        {
            SCString ContinuousSymbol;
            sc.GetContinuousContractSymbolForBarIndex(BarIndex, ContinuousSymbol);
            if (ContinuousSymbol.GetLength() > 0)
                Symbol = ContinuousSymbol;
        }

        return std::string(Symbol.GetChars());
    }

    // Sierra's GetInternalDateTime() is protected in current builds.
    // GetAsDouble() is the documented public accessor. Convert its
    // Excel-style day value back to Sierra's 64-bit microseconds-since-epoch
    // representation used by .depth records.
    int64_t SCDateTimeToInternalMicroseconds(SCDateTime DateTime)
    {
        static const double MICROSECONDS_PER_DAY = 86400000000.0;
        return static_cast<int64_t>(std::llround(DateTime.GetAsDouble() * MICROSECONDS_PER_DAY));
    }

    int64_t ChartDateTimeToUTCInternal(SCStudyInterfaceRef sc, const SCDateTime& ChartDateTime)
    {
        SCDateTime UTC = sc.ConvertDateTimeFromChartTimeZone(ChartDateTime, TIMEZONE_UTC);
        return SCDateTimeToInternalMicroseconds(UTC);
    }

    SCDateTime UTCInternalToChartDateTime(SCStudyInterfaceRef sc, const int64_t UTCInternal)
    {
        SCDateTime UTC;
        UTC.SetInternalDateTime(UTCInternal);
        return sc.ConvertDateTimeToChartTimeZone(UTC, TIMEZONE_UTC);
    }

    void ResetBarMapper(EngineState& S)
    {
        S.MapBarIndex = -1;
        S.NextBarStartUTC = INT64_MAX;
        S.LastMappedRecordUTC = 0;
    }

    int MapRecordToBar(
        SCStudyInterfaceRef sc,
        EngineState& S,
        const int64_t RecordUTC)
    {
        if (sc.ArraySize <= 0)
            return -1;

        const int64_t FirstBarUTC = ChartDateTimeToUTCInternal(sc, sc.BaseDateTimeIn[0]);
        if (RecordUTC < FirstBarUTC)
            return -1;

        // First record, rewind/out-of-order timestamp, or a chart reset:
        // use Sierra's containing-index lookup once to re-anchor.
        if (S.MapBarIndex < 0
            || S.MapBarIndex >= sc.ArraySize
            || (S.LastMappedRecordUTC != 0 && RecordUTC < S.LastMappedRecordUTC))
        {
            const SCDateTime ChartDT = UTCInternalToChartDateTime(sc, RecordUTC);
            S.MapBarIndex = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, ChartDT);
            if (S.MapBarIndex < 0)
                return -1;

            if (S.MapBarIndex + 1 < sc.ArraySize)
                S.NextBarStartUTC = ChartDateTimeToUTCInternal(sc, sc.BaseDateTimeIn[S.MapBarIndex + 1]);
            else
                S.NextBarStartUTC = INT64_MAX;
        }
        else
        {
            // A new bar may have appeared since the previous study call.
            if (S.NextBarStartUTC == INT64_MAX && S.MapBarIndex + 1 < sc.ArraySize)
                S.NextBarStartUTC = ChartDateTimeToUTCInternal(sc, sc.BaseDateTimeIn[S.MapBarIndex + 1]);

            while (S.MapBarIndex + 1 < sc.ArraySize && RecordUTC >= S.NextBarStartUTC)
            {
                ++S.MapBarIndex;

                if (S.MapBarIndex + 1 < sc.ArraySize)
                    S.NextBarStartUTC = ChartDateTimeToUTCInternal(sc, sc.BaseDateTimeIn[S.MapBarIndex + 1]);
                else
                    S.NextBarStartUTC = INT64_MAX;
            }
        }

        S.LastMappedRecordUTC = RecordUTC;
        return S.MapBarIndex;
    }

    int64_t PriceToTickKey(const float Price, const double TickSize)
    {
        if (TickSize <= 0.0)
            return static_cast<int64_t>(std::llround(static_cast<double>(Price) * 1000000.0));

        return static_cast<int64_t>(std::llround(static_cast<double>(Price) / TickSize));
    }

    void ApplyStateOnlyRecord(
        EngineState& S,
        const DepthRecordRaw& R,
        const double TickSize)
    {
        if (R.Command == CMD_CLEAR_BOOK)
        {
            S.BidBook.clear();
            S.AskBook.clear();
            S.HaveValidBook = false;
            S.InSnapshot = true;

            if ((R.Flags & FLAG_END_OF_BATCH) != 0)
            {
                S.InSnapshot = false;
                S.HaveValidBook = true;
            }
            return;
        }

        const bool IsBid = R.Command == CMD_ADD_BID
            || R.Command == CMD_MODIFY_BID
            || R.Command == CMD_DELETE_BID;
        const bool IsAsk = R.Command == CMD_ADD_ASK
            || R.Command == CMD_MODIFY_ASK
            || R.Command == CMD_DELETE_ASK;

        if (!IsBid && !IsAsk)
            return;

        std::unordered_map<int64_t, uint32_t>& Book = IsBid ? S.BidBook : S.AskBook;
        const int64_t PriceKey = PriceToTickKey(R.Price, TickSize);

        if (R.Command == CMD_DELETE_BID || R.Command == CMD_DELETE_ASK)
            Book.erase(PriceKey);
        else
            Book[PriceKey] = R.Quantity;

        if (S.InSnapshot && (R.Flags & FLAG_END_OF_BATCH) != 0)
        {
            S.InSnapshot = false;
            S.HaveValidBook = true;
        }
    }

    uint64_t ApplyAndMeasureRecord(
        EngineState& S,
        const DepthRecordRaw& R,
        const double TickSize)
    {
        if (R.Command == CMD_CLEAR_BOOK)
        {
            ApplyStateOnlyRecord(S, R, TickSize);
            return 0;
        }

        // ADD records immediately following CLEAR_BOOK are the periodic/full
        // snapshot used to reconstruct state. They are not market "effort" and
        // therefore are deliberately not counted.
        if (S.InSnapshot || !S.HaveValidBook)
        {
            ApplyStateOnlyRecord(S, R, TickSize);
            return 0;
        }

        const bool IsBid = R.Command == CMD_ADD_BID
            || R.Command == CMD_MODIFY_BID
            || R.Command == CMD_DELETE_BID;
        const bool IsAsk = R.Command == CMD_ADD_ASK
            || R.Command == CMD_MODIFY_ASK
            || R.Command == CMD_DELETE_ASK;

        if (!IsBid && !IsAsk)
            return 0;

        std::unordered_map<int64_t, uint32_t>& Book = IsBid ? S.BidBook : S.AskBook;
        const int64_t PriceKey = PriceToTickKey(R.Price, TickSize);

        std::unordered_map<int64_t, uint32_t>::iterator It = Book.find(PriceKey);
        const uint32_t OldQuantity = It != Book.end() ? It->second : 0;
        uint64_t GrossContractsChanged = 0;

        if (R.Command == CMD_DELETE_BID || R.Command == CMD_DELETE_ASK)
        {
            GrossContractsChanged = static_cast<uint64_t>(OldQuantity);
            if (It != Book.end())
                Book.erase(It);
        }
        else
        {
            GrossContractsChanged = AbsDiffU32(OldQuantity, R.Quantity);
            Book[PriceKey] = R.Quantity;
        }

        return GrossContractsChanged;
    }

    bool FillReadBuffer(EngineState& S)
    {
        if (S.FileHandle == INVALID_HANDLE_VALUE)
            return false;

        // Keep any partial record from a live file append.
        const size_t Remaining = S.BufferSize > S.BufferPos ? S.BufferSize - S.BufferPos : 0;
        if (Remaining > 0 && S.BufferPos > 0)
            std::memmove(S.ReadBuffer.data(), S.ReadBuffer.data() + S.BufferPos, Remaining);

        S.BufferPos = 0;
        S.BufferSize = Remaining;

        const size_t CapacityRemaining = S.ReadBuffer.size() - Remaining;
        if (CapacityRemaining == 0)
            return Remaining >= sizeof(DepthRecordRaw);

        DWORD BytesRead = 0;
        if (!ReadFile(
                S.FileHandle,
                S.ReadBuffer.data() + Remaining,
                static_cast<DWORD>(CapacityRemaining),
                &BytesRead,
                NULL))
        {
            return Remaining >= sizeof(DepthRecordRaw);
        }

        S.BufferSize += BytesRead;
        return S.BufferSize >= sizeof(DepthRecordRaw);
    }

    // Process every individual recorded .depth command up to TargetUTC.
    // There is no time-window aggregation inside the measurement. The only
    // aggregation is the unavoidable chart display: exact event magnitudes are
    // accumulated into the chart bar whose time interval contains the event.
    void ProcessRecordsThrough(
        SCStudyInterfaceRef sc,
        EngineState& S,
        SCSubgraphRef FullLOBChange,
        const int64_t TargetUTC)
    {
        if (S.FileHandle == INVALID_HANDLE_VALUE)
            return;

        const double TickSize = sc.TickSize > 0.0 ? sc.TickSize : 0.0;

        for (;;)
        {
            if (S.BufferSize - S.BufferPos < sizeof(DepthRecordRaw))
            {
                if (!FillReadBuffer(S))
                    return;
            }

            if (S.BufferSize - S.BufferPos < sizeof(DepthRecordRaw))
                return;

            DepthRecordRaw Record;
            std::memcpy(&Record, S.ReadBuffer.data() + S.BufferPos, sizeof(Record));

            // Keep this future record in the buffer. It will be consumed when
            // replay/live time reaches its exact recorded UTC timestamp.
            if (Record.DateTimeUTC > TargetUTC)
                return;

            S.BufferPos += sizeof(DepthRecordRaw);

            const uint64_t GrossChange = ApplyAndMeasureRecord(S, Record, TickSize);
            S.LastProcessedRecordUTC = Record.DateTimeUTC;

            if (GrossChange == 0)
                continue;

            const int BarIndex = MapRecordToBar(sc, S, Record.DateTimeUTC);
            if (BarIndex >= 0 && BarIndex < sc.ArraySize)
                FullLOBChange[BarIndex] += static_cast<float>(GrossChange);
        }
    }

    bool OpenAtSnapshotForTarget(
        SCStudyInterfaceRef sc,
        EngineState& S,
        const std::string& Path,
        const std::string& Symbol,
        const int UTCDateKey,
        const int64_t TargetUTC)
    {
        if (!OpenAndValidateDepthFile(sc, S, Path, Symbol, UTCDateKey))
            return false;

        const uint64_t LastAtTarget = FindLastRecordAtOrBefore(S.FileHandle, S.Header, TargetUTC);
        const uint64_t SnapshotIndex = FindPreviousSnapshotStart(S.FileHandle, S.Header, LastAtTarget);
        const uint64_t Offset = static_cast<uint64_t>(S.Header.HeaderSize)
            + SnapshotIndex * static_cast<uint64_t>(S.Header.RecordSize);

        if (!SeekAbsolute(S.FileHandle, Offset))
        {
            CloseDepthFile(S);
            return false;
        }

        S.BufferPos = 0;
        S.BufferSize = 0;
        S.BidBook.clear();
        S.AskBook.clear();
        S.HaveValidBook = false;
        S.InSnapshot = false;
        S.LastProcessedRecordUTC = 0;
        ResetBarMapper(S);
        return true;
    }

    bool OpenFromStartPreservingBook(
        SCStudyInterfaceRef sc,
        EngineState& S,
        const std::string& Path,
        const std::string& Symbol,
        const int UTCDateKey)
    {
        if (!OpenAndValidateDepthFile(sc, S, Path, Symbol, UTCDateKey))
            return false;

        if (!SeekAbsolute(S.FileHandle, S.Header.HeaderSize))
        {
            CloseDepthFile(S);
            return false;
        }

        S.BufferPos = 0;
        S.BufferSize = 0;
        return true;
    }

    void ClearOutput(SCStudyInterfaceRef sc, SCSubgraphRef FullLOBChange)
    {
        for (int Index = 0; Index < sc.ArraySize; ++Index)
            FullLOBChange[Index] = 0.0f;
    }
}

SCSFExport scsf_FullLOBChangeRawDepthEvents(SCStudyInterfaceRef sc)
{
    SCSubgraphRef FullLOBChange = sc.Subgraph[0];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Full LOB Change - Raw Depth Events";
        sc.StudyDescription = "Reads Sierra Chart .depth files directly and processes each recorded market-depth command. Output is gross displayed contracts changed across the full recorded LOB: for each actual ADD/MODIFY/DELETE record, the absolute quantity change at that price is accumulated into the containing chart bar. Periodic full-book snapshot/reset records are used only to reconstruct state and are not counted. No smoothing, rolling window, normalization, depth-level limit, directional interpretation, trade data, or inputs.";

        sc.AutoLoop = 0;
        sc.GraphRegion = 1;
        sc.FreeDLL = 0;
        sc.UpdateAlways = 1;

        // This study reads the recorded .depth event file directly. Setting
        // this also ensures Sierra maintains/requests historical depth data
        // when the study is present.
        sc.UsesMarketDepthData = 1;
        sc.MaintainHistoricalMarketDepthData = 1;

        FullLOBChange.Name = "Full LOB Change (raw depth events / bar)";
        FullLOBChange.DrawStyle = DRAWSTYLE_LINE;
        FullLOBChange.LineWidth = 2;
        FullLOBChange.DrawZeros = true;

        return;
    }

    EngineState* State = static_cast<EngineState*>(sc.GetPersistentPointer(1));
    if (State == NULL)
    {
        State = new EngineState;
        sc.SetPersistentPointer(1, State);
    }

    if (sc.LastCallToFunction)
    {
        CloseDepthFile(*State);
        delete State;
        sc.SetPersistentPointer(1, NULL);
        return;
    }

    if (sc.ArraySize <= 0 || sc.ContinuousFuturesContractLoading)
        return;

    // During replay, use the latest chart-data record time rather than the
    // calculated replay wall clock. This prevents a fast replay clock from
    // running ahead of bars that Sierra has actually built. In live mode we
    // use chart current time so depth events between trades are still visible.
    SCDateTime TargetChartTime;
    if (sc.IsReplayRunning())
        TargetChartTime = sc.LatestDateTimeForLastBar;
    else
        TargetChartTime = sc.GetCurrentDateTime();

    // SCDateTime supports direct comparison; avoid the protected internal getter.
    if (TargetChartTime == SCDateTime())
        return;

    const SCDateTime TargetUTCDateTime = sc.ConvertDateTimeFromChartTimeZone(TargetChartTime, TIMEZONE_UTC);
    const int64_t TargetUTC = SCDateTimeToInternalMicroseconds(TargetUTCDateTime);

    int Year = 0;
    int Month = 0;
    int Day = 0;
    TargetUTCDateTime.GetDateYMD(Year, Month, Day);
    const int UTCDateKey = Year * 10000 + Month * 100 + Day;

    const std::string DesiredSymbol = ResolveDepthSymbolForTime(sc, TargetChartTime);
    const std::string DesiredPath = BuildDepthFilePath(sc, DesiredSymbol, Year, Month, Day);

    const bool FullReset = sc.IsFullRecalculation
        || (State->LastTargetUTC != 0 && TargetUTC < State->LastTargetUTC);

    if (FullReset)
    {
        CloseDepthFile(*State);
        State->BidBook.clear();
        State->AskBook.clear();
        State->HaveValidBook = false;
        State->InSnapshot = false;
        State->LastProcessedRecordUTC = 0;
        ResetBarMapper(*State);
        ClearOutput(sc, FullLOBChange);
    }

    const bool NeedDifferentFile = State->FileHandle == INVALID_HANDLE_VALUE
        || State->OpenPath != DesiredPath;

    if (NeedDifferentFile)
    {
        // Normal UTC-day rollover with the same futures contract: finish the
        // old day's file and preserve the reconstructed book into the new
        // file. This avoids an artificial state break at 00:00 UTC.
        const bool ForwardSameSymbolDateRollover =
            State->FileHandle != INVALID_HANDLE_VALUE
            && State->OpenSymbol == DesiredSymbol
            && UTCDateKey > State->OpenUTCDateKey
            && State->LastTargetUTC != 0
            && TargetUTC >= State->LastTargetUTC;

        if (ForwardSameSymbolDateRollover)
        {
            ProcessRecordsThrough(sc, *State, FullLOBChange, INT64_MAX);

            if (!OpenFromStartPreservingBook(
                    sc,
                    *State,
                    DesiredPath,
                    DesiredSymbol,
                    UTCDateKey))
            {
                State->LastTargetUTC = TargetUTC;
                return;
            }
        }
        else
        {
            if (!OpenAtSnapshotForTarget(
                    sc,
                    *State,
                    DesiredPath,
                    DesiredSymbol,
                    UTCDateKey,
                    TargetUTC))
            {
                State->LastTargetUTC = TargetUTC;
                return;
            }
        }
    }

    ProcessRecordsThrough(sc, *State, FullLOBChange, TargetUTC);
    State->LastTargetUTC = TargetUTC;
}
