#include "sierrachart.h"

SCDLLName("gc: Bid Ask Volume Side-by-Side")

/*==========================================================================*/
// Forward declaration of the GDI drawing function
void DrawBidAskVolumeBars(HWND WindowHandle, HDC DeviceContext, SCStudyInterfaceRef sc);

/*==========================================================================*/
SCSFExport scsf_gcBidAskVolumeSideBySide(SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_AskVolume = sc.Subgraph[1];
	SCSubgraphRef Subgraph_BidVolume = sc.Subgraph[2];
	SCSubgraphRef Subgraph_ZeroLine = sc.Subgraph[3];

	// Bar Appearance Settings (1-6)
	SCInputRef Input_BidVolumeColor = sc.Input[1];
	SCInputRef Input_AskVolumeColor = sc.Input[2];
	SCInputRef Input_DimPreviousBars = sc.Input[3];
	SCInputRef Input_SwapBidAskPositions = sc.Input[4];
	SCInputRef Input_OutlineThickness = sc.Input[5];
	SCInputRef Input_BarSpacingPixels = sc.Input[6];

	// Bid Line Settings (7-10)
	SCInputRef Input_ShowBidLine = sc.Input[7];
	SCInputRef Input_BidLineColor = sc.Input[8];
	SCInputRef Input_BidLineWidth = sc.Input[9];
	SCInputRef Input_BidLineStyle = sc.Input[10];

	// Ask Line Settings (11-14)
	SCInputRef Input_ShowAskLine = sc.Input[11];
	SCInputRef Input_AskLineColor = sc.Input[12];
	SCInputRef Input_AskLineWidth = sc.Input[13];
	SCInputRef Input_AskLineStyle = sc.Input[14];

	// Line Offset (15)
	SCInputRef Input_LineOffsetBars = sc.Input[15];

	// Container Settings (16-22)
	SCInputRef Input_ShowContainerOutline = sc.Input[16];
	SCInputRef Input_ContainerOutlineColor = sc.Input[17];
	SCInputRef Input_ContainerOutlineWidth = sc.Input[18];
	SCInputRef Input_ContainerOutlineStyle = sc.Input[19];
	SCInputRef Input_ContainerPadding = sc.Input[20];
	SCInputRef Input_FillContainer = sc.Input[21];
	SCInputRef Input_ContainerFillColor = sc.Input[22];

	// Advanced (23)
	SCInputRef Input_TryUseNonzeroVolumes = sc.Input[23];

	if (sc.SetDefaults)
	{
		sc.GraphName = "gc: Bid Ask Volume Side-by-Side";
		sc.ValueFormat = 0;
		sc.GraphRegion = 1;

		Subgraph_ZeroLine.Name = "Zero Line";
		Subgraph_ZeroLine.PrimaryColor = RGB(0, 0, 0);
		Subgraph_ZeroLine.DrawStyle = DRAWSTYLE_LINE;
		Subgraph_ZeroLine.DrawZeros = true;

		// === Bar Appearance Settings ===
		Input_BidVolumeColor.Name = "Bid Volume Color";
		Input_BidVolumeColor.SetColor(RGB(255, 0, 0)); // Red

		Input_AskVolumeColor.Name = "Ask Volume Color";
		Input_AskVolumeColor.SetColor(RGB(0, 255, 0)); // Green

		Input_DimPreviousBars.Name = "Dim Previous Bars (%)";
		Input_DimPreviousBars.SetInt(0);
		Input_DimPreviousBars.SetIntLimits(0, 90);

		Input_SwapBidAskPositions.Name = "Swap Bid/Ask Positions";
		Input_SwapBidAskPositions.SetYesNo(0);

		Input_OutlineThickness.Name = "Bar Outline Thickness";
		Input_OutlineThickness.SetInt(1);
		Input_OutlineThickness.SetIntLimits(1, 10);

		Input_BarSpacingPixels.Name = "Space Between Bid/Ask Bars (pixels)";
		Input_BarSpacingPixels.SetInt(0);
		Input_BarSpacingPixels.SetIntLimits(0, 20);

		// === Bid Line Settings ===
		Input_ShowBidLine.Name = "Show Bid Volume Line";
		Input_ShowBidLine.SetYesNo(0);

		Input_BidLineColor.Name = "Bid Line Color";
		Input_BidLineColor.SetColor(RGB(255, 0, 0)); // Red

		Input_BidLineWidth.Name = "Bid Line Width";
		Input_BidLineWidth.SetInt(1);
		Input_BidLineWidth.SetIntLimits(1, 10);

		Input_BidLineStyle.Name = "Bid Line Style";
		Input_BidLineStyle.SetCustomInputStrings("Solid;Dash;Dot;Dash Dot;Dash Dot Dot");
		Input_BidLineStyle.SetCustomInputIndex(0);

		// === Ask Line Settings ===
		Input_ShowAskLine.Name = "Show Ask Volume Line";
		Input_ShowAskLine.SetYesNo(0);

		Input_AskLineColor.Name = "Ask Line Color";
		Input_AskLineColor.SetColor(RGB(0, 255, 0)); // Green

		Input_AskLineWidth.Name = "Ask Line Width";
		Input_AskLineWidth.SetInt(1);
		Input_AskLineWidth.SetIntLimits(1, 10);

		Input_AskLineStyle.Name = "Ask Line Style";
		Input_AskLineStyle.SetCustomInputStrings("Solid;Dash;Dot;Dash Dot;Dash Dot Dot");
		Input_AskLineStyle.SetCustomInputIndex(0);

		// === Line Offset ===
		Input_LineOffsetBars.Name = "Line Start Offset (bars to right)";
		Input_LineOffsetBars.SetInt(0);
		Input_LineOffsetBars.SetIntLimits(0, 50);

		// === Container Settings ===
		Input_ShowContainerOutline.Name = "Show Container Outline";
		Input_ShowContainerOutline.SetYesNo(0);

		Input_ContainerOutlineColor.Name = "Container Outline Color";
		Input_ContainerOutlineColor.SetColor(RGB(128, 128, 128)); // Gray

		Input_ContainerOutlineWidth.Name = "Container Outline Width";
		Input_ContainerOutlineWidth.SetInt(1);
		Input_ContainerOutlineWidth.SetIntLimits(1, 10);

		Input_ContainerOutlineStyle.Name = "Container Outline Style";
		Input_ContainerOutlineStyle.SetCustomInputStrings("Solid;Dash;Dot;Dash Dot;Dash Dot Dot");
		Input_ContainerOutlineStyle.SetCustomInputIndex(0);

		Input_ContainerPadding.Name = "Container Padding (pixels)";
		Input_ContainerPadding.SetInt(0);
		Input_ContainerPadding.SetIntLimits(0, 10);

		Input_FillContainer.Name = "Fill Container";
		Input_FillContainer.SetYesNo(0);

		Input_ContainerFillColor.Name = "Container Fill Color";
		Input_ContainerFillColor.SetColor(RGB(50, 50, 50)); // Dark gray

		// === Advanced ===
		Input_TryUseNonzeroVolumes.Name = "Try Use Non-Zero Bid/Ask Volumes for Difference";
		Input_TryUseNonzeroVolumes.SetYesNo(0);

		sc.AutoLoop = 0; // Manual looping

		return;
	}

	Subgraph_AskVolume.Name = "Ask Volume";
	Subgraph_AskVolume.DrawStyle = DRAWSTYLE_HIDDEN; // Hidden for scaling only
	Subgraph_AskVolume.PrimaryColor = RGB(0, 0, 0);
	Subgraph_AskVolume.DrawZeros = true;

	Subgraph_BidVolume.Name = "Bid Volume";
	Subgraph_BidVolume.DrawStyle = DRAWSTYLE_HIDDEN; // Hidden for scaling only
	Subgraph_BidVolume.PrimaryColor = RGB(0, 0, 0);
	Subgraph_BidVolume.DrawZeros = true;

	// Set the GDI drawing function pointer
	sc.p_GDIFunction = DrawBidAskVolumeBars;

	// Check for the appropriate chart type
	if (sc.ChartDataType != INTRADAY_DATA)
	{
		if (sc.Index == 0)
		{
			sc.AddMessageToLog("This study can only be applied to an Intraday Chart", 1);
		}
		return;
	}

	int StartIndex = sc.UpdateStartIndex;

	// Loop through the new indexes to fill in subgraph values (for scaling)
	for (int DestIndex = StartIndex; DestIndex < sc.ArraySize; ++DestIndex)
	{
		float BidVolumeVal = sc.BidVolume[DestIndex];
		float AskVolumeVal = sc.AskVolume[DestIndex];

		Subgraph_AskVolume[DestIndex] = AskVolumeVal;
		Subgraph_BidVolume[DestIndex] = BidVolumeVal;
		Subgraph_ZeroLine[DestIndex] = 0;

		if (Input_TryUseNonzeroVolumes.GetYesNo() == 1)
		{
			// if both volume are zero skip this iteration
			if (BidVolumeVal == 0 && AskVolumeVal == 0)
				continue;

			// if Bid Volume is zero try to find nonzero volume in previous elements
			if (BidVolumeVal == 0)
			{
				int Index = DestIndex - 1;
				while (Index >= 0 && BidVolumeVal == 0)
				{
					BidVolumeVal = sc.BidVolume[Index];
					Index--;
				}
			}

			// if Ask Volume is zero try to find nonzero volume in previous elements
			if (AskVolumeVal == 0)
			{
				int Index = DestIndex - 1;
				while (Index >= 0 && AskVolumeVal == 0)
				{
					AskVolumeVal = sc.AskVolume[Index];
					Index--;
				}
			}
		}
	}
}

/*==========================================================================*/
// GDI Drawing Function
void DrawBidAskVolumeBars(HWND WindowHandle, HDC DeviceContext, SCStudyInterfaceRef sc)
{
	SCSubgraphRef Subgraph_AskVolume = sc.Subgraph[1];
	SCSubgraphRef Subgraph_BidVolume = sc.Subgraph[2];

	// Bar Appearance Settings (1-6)
	SCInputRef Input_BidVolumeColor = sc.Input[1];
	SCInputRef Input_AskVolumeColor = sc.Input[2];
	SCInputRef Input_DimPreviousBars = sc.Input[3];
	SCInputRef Input_SwapBidAskPositions = sc.Input[4];
	SCInputRef Input_OutlineThickness = sc.Input[5];
	SCInputRef Input_BarSpacingPixels = sc.Input[6];

	// Bid Line Settings (7-10)
	SCInputRef Input_ShowBidLine = sc.Input[7];
	SCInputRef Input_BidLineColor = sc.Input[8];
	SCInputRef Input_BidLineWidth = sc.Input[9];
	SCInputRef Input_BidLineStyle = sc.Input[10];

	// Ask Line Settings (11-14)
	SCInputRef Input_ShowAskLine = sc.Input[11];
	SCInputRef Input_AskLineColor = sc.Input[12];
	SCInputRef Input_AskLineWidth = sc.Input[13];
	SCInputRef Input_AskLineStyle = sc.Input[14];

	// Line Offset (15)
	SCInputRef Input_LineOffsetBars = sc.Input[15];

	// Container Settings (16-22)
	SCInputRef Input_ShowContainerOutline = sc.Input[16];
	SCInputRef Input_ContainerOutlineColor = sc.Input[17];
	SCInputRef Input_ContainerOutlineWidth = sc.Input[18];
	SCInputRef Input_ContainerOutlineStyle = sc.Input[19];
	SCInputRef Input_ContainerPadding = sc.Input[20];
	SCInputRef Input_FillContainer = sc.Input[21];
	SCInputRef Input_ContainerFillColor = sc.Input[22];

	// Set clipping region to prevent drawing outside study region
	n_ACSIL::s_GraphicsRectangle ClipRect;
	ClipRect.Left = sc.StudyRegionLeftCoordinate;
	ClipRect.Top = sc.StudyRegionTopCoordinate;
	ClipRect.Right = sc.StudyRegionRightCoordinate;
	ClipRect.Bottom = sc.StudyRegionBottomCoordinate;
	sc.Graphics.SetClippingRegionFromRectangle(ClipRect);

	// Check if positions should be swapped
	bool SwapPositions = Input_SwapBidAskPositions.GetYesNo() != 0;

	// Get outline thickness and bar spacing
	int OutlineThickness = Input_OutlineThickness.GetInt();
	int BarSpacing = Input_BarSpacingPixels.GetInt();

	// Get visible bar range
	int FirstVisibleBar = sc.IndexOfFirstVisibleBar;
	int LastVisibleBar = sc.IndexOfLastVisibleBar;

	// Get dimming percentage
	int DimPercent = Input_DimPreviousBars.GetInt();

	// Convert line style index to pen style - defined here for reuse
	auto GetPenStyle = [](int StyleIndex) -> n_ACSIL::s_GraphicsPen::e_PenStyle {
		switch (StyleIndex)
		{
		case 0: return n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_SOLID;
		case 1: return n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_DASH;
		case 2: return n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_DOT;
		case 3: return n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_DASHDOT;
		case 4: return n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_DASHDOTDOT;
		default: return n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_SOLID;
		}
		};

	// Draw only visible bars
	for (int BarIndex = FirstVisibleBar; BarIndex <= LastVisibleBar && BarIndex < sc.ArraySize; ++BarIndex)
	{
		if (BarIndex < 0)
			continue;

		// Get X pixel coordinates for this bar and next bar
		int BarCenterX = sc.BarIndexToXPixelCoordinate(BarIndex);
		int NextBarCenterX = sc.BarIndexToXPixelCoordinate(BarIndex + 1);

		// Calculate bar width in pixels
		int BarWidthPixels = NextBarCenterX - BarCenterX;
		int QuarterBarWidth = BarWidthPixels / 4;

		// Get volume values
		float BidVolumeVal = Subgraph_BidVolume[BarIndex];
		float AskVolumeVal = Subgraph_AskVolume[BarIndex];

		// Get Y pixel coordinates
		int ZeroY = sc.RegionValueToYPixelCoordinate(0, sc.GraphRegion);
		int BidY = sc.RegionValueToYPixelCoordinate(BidVolumeVal, sc.GraphRegion);
		int AskY = sc.RegionValueToYPixelCoordinate(AskVolumeVal, sc.GraphRegion);

		// Calculate X coordinates for the split bars with spacing
		// Default: Bid on left, Ask on right
		// Apply half the spacing to each side of center
		int HalfSpacing = BarSpacing / 2;

		int LeftBarLeftX = BarCenterX - QuarterBarWidth;
		int LeftBarRightX = BarCenterX - HalfSpacing;
		int RightBarLeftX = BarCenterX + HalfSpacing;
		int RightBarRightX = BarCenterX + QuarterBarWidth;

		// Get colors from inputs
		COLORREF BidColor = Input_BidVolumeColor.GetColor();
		COLORREF AskColor = Input_AskVolumeColor.GetColor();

		// Apply dimming to previous bars (not the last visible bar)
		if (DimPercent > 0 && BarIndex != LastVisibleBar)
		{
			float DimFactor = (100.0f - DimPercent) / 100.0f;

			// Dim Bid Color
			int BidR = (int)(GetRValue(BidColor) * DimFactor);
			int BidG = (int)(GetGValue(BidColor) * DimFactor);
			int BidB = (int)(GetBValue(BidColor) * DimFactor);
			BidColor = RGB(BidR, BidG, BidB);

			// Dim Ask Color
			int AskR = (int)(GetRValue(AskColor) * DimFactor);
			int AskG = (int)(GetGValue(AskColor) * DimFactor);
			int AskB = (int)(GetBValue(AskColor) * DimFactor);
			AskColor = RGB(AskR, AskG, AskB);
		}

		// Determine which volume goes where based on swap setting
		float LeftVolumeVal = SwapPositions ? AskVolumeVal : BidVolumeVal;
		int LeftY = SwapPositions ? AskY : BidY;
		COLORREF LeftColor = SwapPositions ? AskColor : BidColor;

		float RightVolumeVal = SwapPositions ? BidVolumeVal : AskVolumeVal;
		int RightY = SwapPositions ? BidY : AskY;
		COLORREF RightColor = SwapPositions ? BidColor : AskColor;

		// Draw Container FIRST (before volume bars, so bars appear on top)
		if (Input_ShowContainerOutline.GetYesNo())
		{
			// Determine the maximum height for the container
			float MaxVolume = (BidVolumeVal > AskVolumeVal) ? BidVolumeVal : AskVolumeVal;

			if (MaxVolume != 0)
			{
				int ContainerTopY = sc.RegionValueToYPixelCoordinate(MaxVolume, sc.GraphRegion);

				// Get user-defined padding
				int Padding = Input_ContainerPadding.GetInt();

				// Container spans from the leftmost edge to the rightmost edge of both bars
				// Apply user-defined padding
				int ContainerLeftX = LeftBarLeftX - Padding;
				int ContainerRightX = RightBarRightX + Padding;
				int ContainerTopYOffset = ContainerTopY - Padding;
				int ContainerBottomYOffset = ZeroY; // Keep bottom at zero line

				// Check if container fill is enabled
				if (Input_FillContainer.GetYesNo())
				{
					// Set up brush with fill color (no transparency support in Sierra Chart GDI)
					COLORREF FillColor = Input_ContainerFillColor.GetColor();

					n_ACSIL::s_GraphicsBrush FillBrush;
					FillBrush.m_BrushType = n_ACSIL::s_GraphicsBrush::BRUSH_TYPE_SOLID;
					FillBrush.m_BrushColor.SetRGB(GetRValue(FillColor), GetGValue(FillColor), GetBValue(FillColor));
					sc.Graphics.SetBrush(FillBrush);
				}
				else
				{
					// Set up hollow brush (no fill)
					n_ACSIL::s_GraphicsBrush HollowBrush;
					HollowBrush.m_BrushType = n_ACSIL::s_GraphicsBrush::BRUSH_TYPE_STOCK;
					HollowBrush.m_BrushStockType = NULL_BRUSH;
					sc.Graphics.SetBrush(HollowBrush);
				}

				// Set up pen for container outline
				COLORREF ContainerColor = Input_ContainerOutlineColor.GetColor();
				n_ACSIL::s_GraphicsPen ContainerPen;
				ContainerPen.m_PenColor.SetRGB(GetRValue(ContainerColor),
					GetGValue(ContainerColor),
					GetBValue(ContainerColor));
				ContainerPen.m_PenStyle = GetPenStyle(Input_ContainerOutlineStyle.GetIndex());
				ContainerPen.m_Width = Input_ContainerOutlineWidth.GetInt();
				sc.Graphics.SetPen(ContainerPen);

				// Draw the container rectangle
				sc.Graphics.DrawRectangle(ContainerLeftX, ContainerTopYOffset, ContainerRightX, ContainerBottomYOffset);
			}
		}

		// Draw Left Volume Bar
		if (LeftVolumeVal != 0)
		{
			// Set brush color
			n_ACSIL::s_GraphicsBrush LeftBrush;
			LeftBrush.m_BrushType = n_ACSIL::s_GraphicsBrush::BRUSH_TYPE_SOLID;
			LeftBrush.m_BrushColor.SetRGB(GetRValue(LeftColor),
				GetGValue(LeftColor),
				GetBValue(LeftColor));
			sc.Graphics.SetBrush(LeftBrush);

			// Set pen for outline
			n_ACSIL::s_GraphicsPen LeftPen;
			LeftPen.m_PenColor.SetRGB(GetRValue(LeftColor),
				GetGValue(LeftColor),
				GetBValue(LeftColor));
			LeftPen.m_PenStyle = n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_SOLID;
			LeftPen.m_Width = OutlineThickness;
			sc.Graphics.SetPen(LeftPen);

			// Draw the filled rectangle
			sc.Graphics.DrawRectangle(LeftBarLeftX, LeftY, LeftBarRightX, ZeroY);
		}

		// Draw Right Volume Bar
		if (RightVolumeVal != 0)
		{
			// Set brush color
			n_ACSIL::s_GraphicsBrush RightBrush;
			RightBrush.m_BrushType = n_ACSIL::s_GraphicsBrush::BRUSH_TYPE_SOLID;
			RightBrush.m_BrushColor.SetRGB(GetRValue(RightColor),
				GetGValue(RightColor),
				GetBValue(RightColor));
			sc.Graphics.SetBrush(RightBrush);

			// Set pen for outline
			n_ACSIL::s_GraphicsPen RightPen;
			RightPen.m_PenColor.SetRGB(GetRValue(RightColor),
				GetGValue(RightColor),
				GetBValue(RightColor));
			RightPen.m_PenStyle = n_ACSIL::s_GraphicsPen::e_PenStyle::PEN_STYLE_SOLID;
			RightPen.m_Width = OutlineThickness;
			sc.Graphics.SetPen(RightPen);

			// Draw the filled rectangle
			sc.Graphics.DrawRectangle(RightBarLeftX, RightY, RightBarRightX, ZeroY);
		}
	}

	// Draw horizontal lines for current bid/ask volume levels (last bar)
	if (LastVisibleBar >= 0 && LastVisibleBar < sc.ArraySize)
	{
		float CurrentBidVolume = Subgraph_BidVolume[LastVisibleBar];
		float CurrentAskVolume = Subgraph_AskVolume[LastVisibleBar];

		// Get line offset in bars
		int LineOffsetBars = Input_LineOffsetBars.GetInt();

		// Calculate starting X position with offset
		int OffsetBarIndex = LastVisibleBar + LineOffsetBars;
		int LineStartX = sc.BarIndexToXPixelCoordinate(OffsetBarIndex);
		int RightEdgeX = sc.StudyRegionRightCoordinate;

		// Draw Bid Volume Line
		if (Input_ShowBidLine.GetYesNo() && CurrentBidVolume != 0)
		{
			int BidY = sc.RegionValueToYPixelCoordinate(CurrentBidVolume, sc.GraphRegion);
			COLORREF BidLineColor = Input_BidLineColor.GetColor();

			n_ACSIL::s_GraphicsPen BidLinePen;
			BidLinePen.m_PenColor.SetRGB(GetRValue(BidLineColor),
				GetGValue(BidLineColor),
				GetBValue(BidLineColor));
			BidLinePen.m_PenStyle = GetPenStyle(Input_BidLineStyle.GetIndex());
			BidLinePen.m_Width = Input_BidLineWidth.GetInt();
			sc.Graphics.SetPen(BidLinePen);

			sc.Graphics.MoveTo(LineStartX, BidY);
			sc.Graphics.LineTo(RightEdgeX, BidY);
		}

		// Draw Ask Volume Line
		if (Input_ShowAskLine.GetYesNo() && CurrentAskVolume != 0)
		{
			int AskY = sc.RegionValueToYPixelCoordinate(CurrentAskVolume, sc.GraphRegion);
			COLORREF AskLineColor = Input_AskLineColor.GetColor();

			n_ACSIL::s_GraphicsPen AskLinePen;
			AskLinePen.m_PenColor.SetRGB(GetRValue(AskLineColor),
				GetGValue(AskLineColor),
				GetBValue(AskLineColor));
			AskLinePen.m_PenStyle = GetPenStyle(Input_AskLineStyle.GetIndex());
			AskLinePen.m_Width = Input_AskLineWidth.GetInt();
			sc.Graphics.SetPen(AskLinePen);

			sc.Graphics.MoveTo(LineStartX, AskY);
			sc.Graphics.LineTo(RightEdgeX, AskY);
		}
	}

	// Reset clipping region when done
	sc.Graphics.ResetClippingRegion();
}