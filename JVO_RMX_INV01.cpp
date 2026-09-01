#include "sierrachart.h"

SCDLLName("JVO RMX INV01")

namespace
{
    const int ROW_COUNT = 8;

    float JvoAbs(float v)
    {
        return v >= 0.0f ? v : -v;
    }

    float JvoClamp(float v, float lo, float hi)
    {
        if (v < lo)
            return lo;
        if (v > hi)
            return hi;
        return v;
    }

    int JvoSign(float v)
    {
        if (v > 0.0f)
            return 1;
        if (v < 0.0f)
            return -1;
        return 0;
    }

    int InterpolateChannel(int startValue, int endValue, float t)
    {
        t = JvoClamp(t, 0.0f, 1.0f);
        return static_cast<int>(startValue + (endValue - startValue) * t + 0.5f);
    }

    COLORREF PositiveHeatColor(float strength)
    {
        const float t = JvoClamp(strength, 0.0f, 1.0f);
        const int r = InterpolateChannel(219, 72, t);
        const int g = InterpolateChannel(240, 181, t);
        const int b = InterpolateChannel(248, 225, t);
        return RGB(r, g, b);
    }

    COLORREF NegativeHeatColor(float strength)
    {
        const float t = JvoClamp(strength, 0.0f, 1.0f);
        const int c = InterpolateChannel(232, 125, t);
        return RGB(c, c, c);
    }

    float GetRotationScore(SCStudyInterfaceRef sc, int index)
    {
        if (index <= 0 || index >= sc.ArraySize)
            return 0.0f;

        float rf = 0.0f;

        if (sc.High[index] > sc.High[index - 1])
            rf += 1.0f;
        else if (sc.High[index] < sc.High[index - 1])
            rf -= 1.0f;

        if (sc.Low[index] > sc.Low[index - 1])
            rf += 1.0f;
        else if (sc.Low[index] < sc.Low[index - 1])
            rf -= 1.0f;

        return JvoClamp(rf * 0.5f, -1.0f, 1.0f);
    }

    float GetDeltaScore(SCStudyInterfaceRef sc, int index)
    {
        if (index < 0 || index >= sc.ArraySize)
            return 0.0f;

        const float ask = sc.AskVolume[index];
        const float bid = sc.BidVolume[index];
        const float total = ask + bid;

        if (total <= 0.0f)
            return 0.0f;

        return JvoClamp((ask - bid) / total, -1.0f, 1.0f);
    }

    // Immediate directional efficiency for the current 10-second bar.
    // This deliberately avoids a long rolling path average. A close near one
    // extreme of the bar is efficient in that direction; a small body in a
    // large range is inefficient/mixed.
    float GetEfficiencyScore(SCStudyInterfaceRef sc, int index)
    {
        if (index < 0 || index >= sc.ArraySize)
            return 0.0f;

        const float range = sc.High[index] - sc.Low[index];
        if (range <= 0.0f)
            return 0.0f;

        return JvoClamp((sc.Close[index] - sc.Open[index]) / range, -1.0f, 1.0f);
    }

    void ClearRow(SCSubgraphRef top, SCSubgraphRef bottom, int index)
    {
        top[index] = 0.0f;
        bottom[index] = 0.0f;
        top.DataColor[index] = top.PrimaryColor;
        bottom.DataColor[index] = bottom.PrimaryColor;
    }
}

SCSFExport scsf_JVORMXINV01(SCStudyInterfaceRef sc)
{
    SCInputRef MinRotation = sc.Input[0];
    SCInputRef DeltaSupport = sc.Input[1];
    SCInputRef EfficiencySupport = sc.Input[2];
    SCInputRef StrongContradiction = sc.Input[3];
    SCInputRef SingleSupportConfidence = sc.Input[4];
    SCInputRef NeutralSupportConfidence = sc.Input[5];
    SCInputRef EvidenceGain = sc.Input[6];
    SCInputRef CounterDepletion = sc.Input[7];
    SCInputRef ExitFraction = sc.Input[8];
    SCInputRef DominanceRatio = sc.Input[9];
    SCInputRef CellHeight = sc.Input[10];

    if (sc.SetDefaults)
    {
        sc.GraphName = "JVO - RMX INV01 (Coherent Directional Inventory)";
        sc.StudyDescription =
            "Inventory-style directional rotation matrix for a 10-second chart. This version abandons rolling multi-horizon averages. "
            "Each bar produces a coherent directional evidence event from Sierra-style price rotation, normalized executed delta, and "
            "immediate price efficiency. Price rotation establishes the candidate direction; delta and efficiency determine whether that "
            "direction is supported, neutral, or contradicted. Eight independent leaky directional inventories then accumulate/deplete that "
            "evidence at different persistence scales. Fast rows forget quickly and should remain relatively sparse; deeper rows require more "
            "accumulated evidence and retain established direction longer. No DOM/depth data is used.";

        sc.AutoLoop = 0;
        sc.GraphRegion = 1;
        sc.ValueFormat = 2;
        sc.ScaleRangeType = SCALE_USERDEFINED;
        sc.ScaleRangeBottom = 0.0f;
        sc.ScaleRangeTop = 8.0f;
        sc.ScaleIncrement = 1.0f;
        sc.DrawStudyUnderneathMainPriceGraph = 0;
        sc.FreeDLL = 0;

        for (int row = 0; row < ROW_COUNT; ++row)
        {
            SCSubgraphRef top = sc.Subgraph[row * 2];
            SCSubgraphRef bottom = sc.Subgraph[row * 2 + 1];

            SCString topName;
            topName.Format("Row %d Top", row + 1);
            top.Name = topName;
            top.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_TOP;
            top.PrimaryColor = RGB(170, 215, 235);
            top.DrawZeros = false;

            SCString bottomName;
            bottomName.Format("Row %d Bottom", row + 1);
            bottom.Name = bottomName;
            bottom.DrawStyle = DRAWSTYLE_FILL_RECTANGLE_BOTTOM;
            bottom.PrimaryColor = RGB(170, 215, 235);
            bottom.DrawZeros = false;
        }

        MinRotation.Name = "Minimum Absolute Price Rotation To Create Evidence";
        MinRotation.SetFloat(0.50f);
        MinRotation.SetFloatLimits(0.0f, 1.0f);

        DeltaSupport.Name = "Delta Alignment Threshold";
        DeltaSupport.SetFloat(0.05f);
        DeltaSupport.SetFloatLimits(0.0f, 1.0f);

        EfficiencySupport.Name = "Efficiency Alignment Threshold";
        EfficiencySupport.SetFloat(0.05f);
        EfficiencySupport.SetFloatLimits(0.0f, 1.0f);

        StrongContradiction.Name = "Strong Contradiction Threshold";
        StrongContradiction.SetFloat(0.25f);
        StrongContradiction.SetFloatLimits(0.0f, 1.0f);

        SingleSupportConfidence.Name = "Confidence When Only One Secondary Input Supports";
        SingleSupportConfidence.SetFloat(0.62f);
        SingleSupportConfidence.SetFloatLimits(0.0f, 1.0f);

        NeutralSupportConfidence.Name = "Confidence When Delta/Efficiency Are Both Neutral";
        NeutralSupportConfidence.SetFloat(0.22f);
        NeutralSupportConfidence.SetFloatLimits(0.0f, 1.0f);

        EvidenceGain.Name = "Directional Evidence Gain";
        EvidenceGain.SetFloat(1.00f);
        EvidenceGain.SetFloatLimits(0.05f, 10.0f);

        CounterDepletion.Name = "Opposite Inventory Depletion Multiplier";
        CounterDepletion.SetFloat(1.35f);
        CounterDepletion.SetFloatLimits(0.0f, 10.0f);

        ExitFraction.Name = "Active State Exit Threshold As Fraction Of Entry";
        ExitFraction.SetFloat(0.55f);
        ExitFraction.SetFloatLimits(0.05f, 0.99f);

        DominanceRatio.Name = "Required Dominance Over Opposite Inventory";
        DominanceRatio.SetFloat(1.15f);
        DominanceRatio.SetFloatLimits(1.0f, 10.0f);

        CellHeight.Name = "Matrix Cell Height";
        CellHeight.SetFloat(0.72f);
        CellHeight.SetFloatLimits(0.10f, 0.95f);

        return;
    }

    if (sc.ArraySize <= 0)
        return;

    // Independent memory scales. These are not rolling lookbacks. Each row is
    // a leaky evidence inventory with progressively longer memory.
    const float Retention[ROW_COUNT] = {
        0.10f, 0.35f, 0.55f, 0.70f, 0.82f, 0.89f, 0.94f, 0.97f
    };

    // Progressively deeper rows require more accumulated coherent evidence.
    const float EntryLevel[ROW_COUNT] = {
        0.60f, 0.85f, 1.10f, 1.45f, 1.90f, 2.60f, 3.60f, 5.00f
    };

    const float minRotation = JvoClamp(MinRotation.GetFloat(), 0.0f, 1.0f);
    const float deltaSupport = JvoClamp(DeltaSupport.GetFloat(), 0.0f, 1.0f);
    const float efficiencySupport = JvoClamp(EfficiencySupport.GetFloat(), 0.0f, 1.0f);
    const float contradiction = JvoClamp(StrongContradiction.GetFloat(), 0.0f, 1.0f);
    const float oneSupportConfidence = JvoClamp(SingleSupportConfidence.GetFloat(), 0.0f, 1.0f);
    const float neutralConfidence = JvoClamp(NeutralSupportConfidence.GetFloat(), 0.0f, 1.0f);
    const float gain = EvidenceGain.GetFloat() > 0.0f ? EvidenceGain.GetFloat() : 1.0f;
    const float counterDepletion = CounterDepletion.GetFloat() >= 0.0f ? CounterDepletion.GetFloat() : 0.0f;
    const float exitFraction = JvoClamp(ExitFraction.GetFloat(), 0.05f, 0.99f);
    const float dominanceRatio = DominanceRatio.GetFloat() >= 1.0f ? DominanceRatio.GetFloat() : 1.0f;
    const float height = JvoClamp(CellHeight.GetFloat(), 0.10f, 0.95f);
    const float halfHeight = height * 0.5f;

    int startIndex = sc.UpdateStartIndex;
    if (startIndex < 0)
        startIndex = 0;

    for (int index = startIndex; index < sc.ArraySize; ++index)
    {
        const float rotation = GetRotationScore(sc, index);
        const float delta = GetDeltaScore(sc, index);
        const float efficiency = GetEfficiencyScore(sc, index);

        int evidenceDirection = 0;
        float evidenceMagnitude = 0.0f;
        float coherence = 0.0f;

        if (JvoAbs(rotation) >= minRotation && JvoAbs(rotation) > 0.0f)
        {
            const int direction = JvoSign(rotation);
            const float alignedDelta = static_cast<float>(direction) * delta;
            const float alignedEfficiency = static_cast<float>(direction) * efficiency;

            const bool deltaStronglyContradicts = alignedDelta <= -contradiction;
            const bool efficiencyStronglyContradicts = alignedEfficiency <= -contradiction;

            // A strong contradiction from either secondary variable means the
            // price rotation is not considered coherent directional evidence.
            if (!deltaStronglyContradicts && !efficiencyStronglyContradicts)
            {
                const bool deltaSupports = alignedDelta >= deltaSupport;
                const bool efficiencySupports = alignedEfficiency >= efficiencySupport;

                if (deltaSupports && efficiencySupports)
                    coherence = 1.0f;
                else if (deltaSupports || efficiencySupports)
                    coherence = oneSupportConfidence;
                else
                    coherence = neutralConfidence;

                // Magnitude rewards strong price rotation first, then stronger
                // aligned delta/efficiency. Opposing-but-not-strong values do
                // not contribute positive strength.
                const float positiveDelta = alignedDelta > 0.0f ? alignedDelta : 0.0f;
                const float positiveEfficiency = alignedEfficiency > 0.0f ? alignedEfficiency : 0.0f;

                const float quality = JvoClamp(
                    0.55f
                    + 0.25f * positiveDelta
                    + 0.20f * positiveEfficiency,
                    0.0f,
                    1.0f);

                evidenceMagnitude = JvoClamp(JvoAbs(rotation) * quality * coherence, 0.0f, 1.0f);

                if (evidenceMagnitude > 0.0f)
                    evidenceDirection = direction;
            }
        }

        for (int row = 0; row < ROW_COUNT; ++row)
        {
            SCSubgraphRef top = sc.Subgraph[row * 2];
            SCSubgraphRef bottom = sc.Subgraph[row * 2 + 1];

            // Internal arrays on each row's Top subgraph:
            // [0] bullish inventory
            // [1] bearish inventory
            // [2] classified state (-1 / 0 / +1)
            // [3] current coherent evidence signed value
            // [4] price rotation score
            // [5] normalized delta score
            // [6] immediate price efficiency score
            // [7] coherence confidence
            // [8] dominant inventory / entry level
            float bullInventory = 0.0f;
            float bearInventory = 0.0f;
            int previousState = 0;

            if (index > 0)
            {
                bullInventory = top.Arrays[0][index - 1] * Retention[row];
                bearInventory = top.Arrays[1][index - 1] * Retention[row];
                previousState = static_cast<int>(top.Arrays[2][index - 1]);
            }

            const float evidence = evidenceMagnitude * gain;

            if (evidenceDirection > 0)
            {
                bearInventory -= evidence * counterDepletion;
                if (bearInventory < 0.0f)
                    bearInventory = 0.0f;
                bullInventory += evidence;
            }
            else if (evidenceDirection < 0)
            {
                bullInventory -= evidence * counterDepletion;
                if (bullInventory < 0.0f)
                    bullInventory = 0.0f;
                bearInventory += evidence;
            }

            const float entry = EntryLevel[row];
            const float exit = entry * exitFraction;

            int state = previousState;

            const bool bullDominant = bullInventory >= bearInventory * dominanceRatio;
            const bool bearDominant = bearInventory >= bullInventory * dominanceRatio;

            if (previousState == 0)
            {
                if (bullInventory >= entry && bullDominant)
                    state = 1;
                else if (bearInventory >= entry && bearDominant)
                    state = -1;
                else
                    state = 0;
            }
            else if (previousState > 0)
            {
                if (bearInventory >= entry && bearDominant)
                    state = -1;
                else if (bullInventory < exit || !bullDominant)
                    state = 0;
                else
                    state = 1;
            }
            else
            {
                if (bullInventory >= entry && bullDominant)
                    state = 1;
                else if (bearInventory < exit || !bearDominant)
                    state = 0;
                else
                    state = -1;
            }

            top.Arrays[0][index] = bullInventory;
            top.Arrays[1][index] = bearInventory;
            top.Arrays[2][index] = static_cast<float>(state);
            top.Arrays[3][index] = static_cast<float>(evidenceDirection) * evidenceMagnitude;
            top.Arrays[4][index] = rotation;
            top.Arrays[5][index] = delta;
            top.Arrays[6][index] = efficiency;
            top.Arrays[7][index] = coherence;

            const float dominantInventory = bullInventory > bearInventory ? bullInventory : bearInventory;
            const float inventoryRatio = entry > 0.0f ? dominantInventory / entry : 0.0f;
            top.Arrays[8][index] = inventoryRatio;

            if (state == 0)
            {
                ClearRow(top, bottom, index);
                continue;
            }

            const float rowCenter = 7.5f - static_cast<float>(row);
            top[index] = rowCenter + halfHeight;
            bottom[index] = rowCenter - halfHeight;

            // Once active, 1.0x entry strength is the lightest visible state.
            // Stronger accumulated inventory gradually darkens the cell.
            const float shadeStrength = JvoClamp((inventoryRatio - 1.0f) / 2.0f, 0.0f, 1.0f);
            const float visibleStrength = 0.18f + 0.82f * shadeStrength;

            COLORREF color = state > 0
                ? PositiveHeatColor(visibleStrength)
                : NegativeHeatColor(visibleStrength);

            top.DataColor[index] = color;
            bottom.DataColor[index] = color;
        }
    }
}
