#include "headers.hpp"

#include "filediff.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include "config.hpp"
#include "codepage.hpp"
#include "colors.hpp"
#include "ctrlobj.hpp"
#include "editor.hpp"
#include "exitcode.hpp"
#include "farcolors.hpp"
#include "farwinapi.hpp"
#include "filepanels.hpp"
#include "filestr.hpp"
#include "format.hpp"
#include "frame.hpp"
#include "interf.hpp"
#include "lang.hpp"
#include "manager.hpp"
#include "message.hpp"
#include "pathmix.hpp"
#include "strmix.hpp"
#include "panel.hpp"

namespace
{
enum class DiffKind
{
	Equal,
	Changed,
	Added,
	Deleted
};

struct DiffRow
{
	int Left = -1;
	int Right = -1;
	DiffKind Kind = DiffKind::Equal;
};

struct DiffHunk
{
	size_t FirstRow = 0;
	size_t LastRow = 0;
};

constexpr size_t InvalidIndex = std::numeric_limits<size_t>::max();

struct ScreenRow
{
	size_t Row = 0;
	size_t Part = 0;
};

struct InlineRange
{
	int Start = 0;
	int End = 0;
};

struct InlineDiff
{
	std::vector<InlineRange> Left;
	std::vector<InlineRange> Right;
};

void StripEol(FARString &Line)
{
	while (!Line.IsEmpty()) {
		const wchar_t Ch = Line.At(Line.GetLength() - 1);
		if (Ch != L'\r' && Ch != L'\n')
			break;
		Line.Truncate(Line.GetLength() - 1);
	}
}

bool LoadTextFile(const FARString &Path, std::vector<FARString> &Lines,
		std::vector<FARString> *EditorLines = nullptr, UINT *DetectedCodePage = nullptr)
{
	File Src;
	if (!Src.Open(Path.CPtr(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING)) {
		return false;
	}

	Lines.clear();
	if (EditorLines)
		EditorLines->clear();

	UINT CodePage = 0;
	bool SignatureFound = false;
	if (!GetFileFormat(Src, CodePage, &SignatureFound, Opt.EdOpt.AutoDetectCodePage != 0) || !IsCodePageSupported(CodePage))
		CodePage = Opt.EdOpt.DefaultCodePage;
	if (DetectedCodePage)
		*DetectedCodePage = CodePage;

	if (!IsUnicodeOrUtfCodePage(CodePage))
		Src.SetPointer(0, nullptr, FILE_BEGIN);

	GetFileString Reader(Src);
	wchar_t *RawLine = nullptr;
	int Length = 0;
	int Result = 0;
	while ((Result = Reader.GetString(&RawLine, CodePage, Length))) {
		if (Result == -1)
			return false;

		FARString LineForEditor(RawLine, Length);
		if (EditorLines)
			EditorLines->emplace_back(LineForEditor);

		FARString Line(LineForEditor);
		StripEol(Line);
		Lines.emplace_back(Line);
	}

	return true;
}

bool ResolvePanelFile(Panel *Source, FARString &Path)
{
	if (!Source || !Source->IsVisible() || Source->GetType() != FILE_PANEL || Source->GetMode() != NORMAL_PANEL)
		return false;

	FARString Name;
	if (!Source->GetCurName(Name) || Name.IsEmpty())
		return false;

	Source->GetCurDir(Path);
	AddEndSlash(Path);
	Path+= Name;

	const DWORD Attr = apiGetFileAttributes(Path.CPtr());
	return Attr != INVALID_FILE_ATTRIBUTES && !(Attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<DiffRow> CoalesceChanges(std::vector<DiffRow> Rows)
{
	std::vector<DiffRow> Result;
	for (size_t I = 0; I < Rows.size();) {
		if (Rows[I].Kind != DiffKind::Deleted) {
			Result.emplace_back(Rows[I++]);
			continue;
		}

		size_t J = I;
		while (J < Rows.size() && Rows[J].Kind == DiffKind::Deleted)
			++J;

		size_t K = J;
		while (K < Rows.size() && Rows[K].Kind == DiffKind::Added)
			++K;

		const size_t DeletedCount = J - I;
		const size_t AddedCount = K - J;
		const size_t ChangedCount = std::min(DeletedCount, AddedCount);
		for (size_t N = 0; N < ChangedCount; ++N)
			Result.push_back({Rows[I + N].Left, Rows[J + N].Right, DiffKind::Changed});
		for (size_t N = ChangedCount; N < DeletedCount; ++N)
			Result.emplace_back(Rows[I + N]);
		for (size_t N = ChangedCount; N < AddedCount; ++N)
			Result.emplace_back(Rows[J + N]);

		I = K;
	}
	return Result;
}

void EmitFallbackRange(const std::vector<FARString> &Left, const std::vector<FARString> &Right,
		size_t LeftBegin, size_t LeftEnd, size_t RightBegin, size_t RightEnd, std::vector<DiffRow> &Rows)
{
	const size_t LeftCount = LeftEnd - LeftBegin;
	const size_t RightCount = RightEnd - RightBegin;
	const size_t Count = std::max(LeftCount, RightCount);
	for (size_t I = 0; I < Count; ++I) {
		const int L = I < LeftCount ? static_cast<int>(LeftBegin + I) : -1;
		const int R = I < RightCount ? static_cast<int>(RightBegin + I) : -1;
		const DiffKind Kind = L < 0 ? DiffKind::Added : R < 0 ? DiffKind::Deleted :
				Left[L] == Right[R] ? DiffKind::Equal : DiffKind::Changed;
		Rows.push_back({L, R, Kind});
	}
}

struct HistogramEntry
{
	size_t Count = 0;
	std::vector<size_t> Positions;
};

struct DiffAnchor
{
	size_t LeftBegin = 0;
	size_t RightBegin = 0;
	size_t Length = 0;
	size_t Frequency = 0;

	bool Valid() const { return Length != 0; }
};

DiffAnchor FindHistogramAnchor(const std::vector<FARString> &Left, const std::vector<FARString> &Right,
		size_t LeftBegin, size_t LeftEnd, size_t RightBegin, size_t RightEnd)
{
	constexpr size_t MaxStoredPositions = 64;
	constexpr size_t MaxUsefulFrequency = 64;

	std::map<FARString, size_t> LeftCounts;
	for (size_t I = LeftBegin; I < LeftEnd; ++I)
		++LeftCounts[Left[I]];

	std::map<FARString, HistogramEntry> RightHistogram;
	for (size_t I = RightBegin; I < RightEnd; ++I) {
		HistogramEntry &Entry = RightHistogram[Right[I]];
		++Entry.Count;
		if (Entry.Positions.size() < MaxStoredPositions)
			Entry.Positions.emplace_back(I);
	}

	DiffAnchor Best;
	Best.Frequency = static_cast<size_t>(-1);

	for (size_t LeftPos = LeftBegin; LeftPos < LeftEnd; ++LeftPos) {
		const auto LeftCount = LeftCounts.find(Left[LeftPos]);
		const auto RightEntry = RightHistogram.find(Left[LeftPos]);
		if (LeftCount == LeftCounts.end() || RightEntry == RightHistogram.end())
			continue;

		const size_t Frequency = LeftCount->second + RightEntry->second.Count;
		if (Frequency > MaxUsefulFrequency || Frequency > Best.Frequency)
			continue;

		for (const size_t RightPos : RightEntry->second.Positions) {
			size_t LB = LeftPos;
			size_t RB = RightPos;
			while (LB > LeftBegin && RB > RightBegin && Left[LB - 1] == Right[RB - 1]) {
				--LB;
				--RB;
			}

			size_t LE = LeftPos + 1;
			size_t RE = RightPos + 1;
			while (LE < LeftEnd && RE < RightEnd && Left[LE] == Right[RE]) {
				++LE;
				++RE;
			}

			const size_t Length = LE - LB;
			if (!Best.Valid() || Frequency < Best.Frequency || (Frequency == Best.Frequency && Length > Best.Length)) {
				Best.LeftBegin = LB;
				Best.RightBegin = RB;
				Best.Length = Length;
				Best.Frequency = Frequency;
			}
		}
	}

	return Best;
}

void BuildHistogramDiff(const std::vector<FARString> &Left, const std::vector<FARString> &Right,
		size_t LeftBegin, size_t LeftEnd, size_t RightBegin, size_t RightEnd, std::vector<DiffRow> &Rows,
		int Depth = 0)
{
	while (LeftBegin < LeftEnd && RightBegin < RightEnd && Left[LeftBegin] == Right[RightBegin]) {
		Rows.push_back({static_cast<int>(LeftBegin++), static_cast<int>(RightBegin++), DiffKind::Equal});
	}

	size_t Suffix = 0;
	while (LeftBegin + Suffix < LeftEnd && RightBegin + Suffix < RightEnd
			&& Left[LeftEnd - Suffix - 1] == Right[RightEnd - Suffix - 1]) {
		++Suffix;
	}
	LeftEnd-= Suffix;
	RightEnd-= Suffix;

	if (LeftBegin == LeftEnd || RightBegin == RightEnd || Depth > 64) {
		EmitFallbackRange(Left, Right, LeftBegin, LeftEnd, RightBegin, RightEnd, Rows);
	} else {
		const DiffAnchor Anchor = FindHistogramAnchor(Left, Right, LeftBegin, LeftEnd, RightBegin, RightEnd);
		if (Anchor.Valid()) {
			BuildHistogramDiff(Left, Right, LeftBegin, Anchor.LeftBegin, RightBegin, Anchor.RightBegin, Rows, Depth + 1);
			for (size_t I = 0; I < Anchor.Length; ++I) {
				Rows.push_back({static_cast<int>(Anchor.LeftBegin + I), static_cast<int>(Anchor.RightBegin + I),
						DiffKind::Equal});
			}
			BuildHistogramDiff(Left, Right, Anchor.LeftBegin + Anchor.Length, LeftEnd,
					Anchor.RightBegin + Anchor.Length, RightEnd, Rows, Depth + 1);
		} else {
			EmitFallbackRange(Left, Right, LeftBegin, LeftEnd, RightBegin, RightEnd, Rows);
		}
	}

	const size_t LeftSuffixBegin = LeftEnd;
	const size_t RightSuffixBegin = RightEnd;
	for (size_t I = 0; I < Suffix; ++I) {
		Rows.push_back({static_cast<int>(LeftSuffixBegin + I), static_cast<int>(RightSuffixBegin + I),
				DiffKind::Equal});
	}
}

std::vector<DiffRow> BuildLineDiff(const std::vector<FARString> &Left, const std::vector<FARString> &Right)
{
	std::vector<DiffRow> Rows;
	Rows.reserve(Left.size() + Right.size());
	BuildHistogramDiff(Left, Right, 0, Left.size(), 0, Right.size(), Rows);
	return CoalesceChanges(std::move(Rows));
}

uint32_t ComposeRgb(int R, int G, int B)
{
	return static_cast<uint32_t>(R) | (static_cast<uint32_t>(G) << 8) | (static_cast<uint32_t>(B) << 16);
}

uint32_t TintRgb(DiffKind Kind)
{
	switch (Kind) {
		case DiffKind::Added:
			return ComposeRgb(90, 170, 110);
		case DiffKind::Deleted:
			return ComposeRgb(220, 90, 90);
		case DiffKind::Changed:
			return ComposeRgb(215, 175, 70);
		case DiffKind::Equal:
		default:
			return 0;
	}
}

uint64_t FallbackDiffBackground(DiffKind Kind, bool Inline)
{
	switch (Kind) {
		case DiffKind::Added:
			return Inline ? B_LIGHTGREEN : B_GREEN;
		case DiffKind::Deleted:
			return Inline ? B_LIGHTRED : B_RED;
		case DiffKind::Changed:
			return Inline ? B_YELLOW : B_BROWN;
		case DiffKind::Equal:
		default:
			return 0;
	}
}

uint64_t ApplyFallbackDiffBackground(uint64_t Attr, DiffKind Kind, bool Inline)
{
	constexpr uint64_t BackgroundTrueColorMask = 0xffffff0000000000ull | BACKGROUND_TRUECOLOR;
	return (Attr & ~(static_cast<uint64_t>(B_MASK) | BackgroundTrueColorMask))
			| FallbackDiffBackground(Kind, Inline);
}

uint32_t IndexedBackgroundRgb(uint64_t Attr)
{
	return Palette::FARPalette[(Attr & B_MASK) >> 4] & 0x00ffffff;
}

bool CanUseTrueColorFallback()
{
	return WINPORT(GetConsoleColorPalette)(NULL) >= 24;
}

uint32_t BlendRgb(uint32_t Base, uint32_t Tint, int Alpha)
{
	const int InvAlpha = 256 - Alpha;
	const int R = (((Base & 0xff) * InvAlpha) + ((Tint & 0xff) * Alpha)) >> 8;
	const int G = ((((Base >> 8) & 0xff) * InvAlpha) + (((Tint >> 8) & 0xff) * Alpha)) >> 8;
	const int B = ((((Base >> 16) & 0xff) * InvAlpha) + (((Tint >> 16) & 0xff) * Alpha)) >> 8;
	return ComposeRgb(R, G, B);
}

uint64_t ApplyDiffTint(uint64_t Attr, DiffKind Kind, bool Inline, bool UseTrueColorFallback)
{
	if (Kind == DiffKind::Equal)
		return Attr;

	uint64_t Result = ApplyFallbackDiffBackground(Attr, Kind, Inline);
	if ((Attr & BACKGROUND_TRUECOLOR) == 0 && !UseTrueColorFallback)
		return Result;

	const uint32_t Base = (Attr & BACKGROUND_TRUECOLOR) ? GET_RGB_BACK(Attr) : IndexedBackgroundRgb(Attr);
	const uint32_t Blended = BlendRgb(Base, TintRgb(Kind), Inline ? 136 : 76);
	SET_RGB_BACK(Result, Blended);
	return Result;
}

void ApplyDiffOverlay(int X, int Y, int Width, DiffKind Kind)
{
	if (Kind == DiffKind::Equal || Width <= 0)
		return;

	std::vector<CHAR_INFO> Buffer(Width);
	GetText(X, Y, X + Width - 1, Y, Buffer.data(), static_cast<int>(Buffer.size() * sizeof(Buffer.front())));
	const bool UseTrueColorFallback = CanUseTrueColorFallback();
	for (CHAR_INFO &Cell : Buffer)
		Cell.Attributes = ApplyDiffTint(Cell.Attributes, Kind, false, UseTrueColorFallback);
	PutText(X, Y, X + Width - 1, Y, Buffer.data());
}

void ApplyInlineDiffOverlay(int X1, int X2, int Y, DiffKind Kind)
{
	if (X1 > X2)
		return;

	const int Width = X2 - X1 + 1;
	std::vector<CHAR_INFO> Buffer(Width);
	GetText(X1, Y, X2, Y, Buffer.data(), static_cast<int>(Buffer.size() * sizeof(Buffer.front())));
	const bool UseTrueColorFallback = CanUseTrueColorFallback();
	for (CHAR_INFO &Cell : Buffer)
		Cell.Attributes = ApplyDiffTint(Cell.Attributes, Kind, true, UseTrueColorFallback);
	PutText(X1, Y, X2, Y, Buffer.data());
}

std::vector<InlineRange> InlineRangesFromMarks(const std::vector<bool> &Marks)
{
	std::vector<InlineRange> Ranges;
	for (size_t I = 0; I < Marks.size();) {
		if (!Marks[I]) {
			++I;
			continue;
		}

		const size_t Start = I;
		while (I < Marks.size() && Marks[I])
			++I;
		Ranges.push_back({static_cast<int>(Start), static_cast<int>(I)});
	}
	return Ranges;
}

void BuildSimpleInlineRanges(const FARString &Left, const FARString &Right,
		std::vector<InlineRange> &LeftRanges, std::vector<InlineRange> &RightRanges)
{
	const int LeftLength = static_cast<int>(Left.GetLength());
	const int RightLength = static_cast<int>(Right.GetLength());
	int Prefix = 0;
	while (Prefix < LeftLength && Prefix < RightLength && Left.At(Prefix) == Right.At(Prefix))
		++Prefix;

	int LeftSuffix = LeftLength;
	int RightSuffix = RightLength;
	while (LeftSuffix > Prefix && RightSuffix > Prefix && Left.At(LeftSuffix - 1) == Right.At(RightSuffix - 1)) {
		--LeftSuffix;
		--RightSuffix;
	}

	if (Prefix < LeftSuffix)
		LeftRanges.push_back({Prefix, LeftSuffix});
	if (Prefix < RightSuffix)
		RightRanges.push_back({Prefix, RightSuffix});
}

void BuildInlineDiffRanges(const FARString &Left, const FARString &Right,
		std::vector<InlineRange> &LeftRanges, std::vector<InlineRange> &RightRanges)
{
	const size_t LeftLength = Left.GetLength();
	const size_t RightLength = Right.GetLength();
	constexpr size_t MaxInlineLcsCells = 65536;
	if (LeftLength == 0 || RightLength == 0
			|| (RightLength != 0 && LeftLength > MaxInlineLcsCells / RightLength)) {
		BuildSimpleInlineRanges(Left, Right, LeftRanges, RightRanges);
		return;
	}

	const size_t Columns = RightLength + 1;
	std::vector<int> Lcs((LeftLength + 1) * Columns);
	for (size_t I = LeftLength; I-- > 0;) {
		for (size_t J = RightLength; J-- > 0;) {
			Lcs[I * Columns + J] = Left.At(I) == Right.At(J)
					? Lcs[(I + 1) * Columns + J + 1] + 1
					: std::max(Lcs[(I + 1) * Columns + J], Lcs[I * Columns + J + 1]);
		}
	}

	std::vector<bool> LeftChanged(LeftLength);
	std::vector<bool> RightChanged(RightLength);
	size_t I = 0;
	size_t J = 0;
	while (I < LeftLength && J < RightLength) {
		if (Left.At(I) == Right.At(J)) {
			++I;
			++J;
		} else if (Lcs[(I + 1) * Columns + J] >= Lcs[I * Columns + J + 1]) {
			LeftChanged[I++] = true;
		} else {
			RightChanged[J++] = true;
		}
	}
	while (I < LeftLength)
		LeftChanged[I++] = true;
	while (J < RightLength)
		RightChanged[J++] = true;

	LeftRanges = InlineRangesFromMarks(LeftChanged);
	RightRanges = InlineRangesFromMarks(RightChanged);
}

bool IsEditorCursorKey(FarKey Key)
{
	switch (Key) {
		case KEY_LEFT:
		case KEY_NUMPAD4:
		case KEY_RIGHT:
		case KEY_NUMPAD6:
		case KEY_UP:
		case KEY_NUMPAD8:
		case KEY_DOWN:
		case KEY_NUMPAD2:
		case KEY_HOME:
		case KEY_NUMPAD7:
		case KEY_END:
		case KEY_NUMPAD1:
		case KEY_PGUP:
		case KEY_NUMPAD9:
		case KEY_PGDN:
		case KEY_NUMPAD3:
		case KEY_CTRLLEFT:
		case KEY_CTRLNUMPAD4:
		case KEY_CTRLRIGHT:
		case KEY_CTRLNUMPAD6:
		case KEY_CTRLHOME:
		case KEY_CTRLNUMPAD7:
		case KEY_CTRLEND:
		case KEY_CTRLNUMPAD1:
		case KEY_SHIFTHOME:
		case KEY_SHIFTNUMPAD7:
		case KEY_SHIFTEND:
		case KEY_SHIFTNUMPAD1:
		case KEY_SHIFTLEFT:
		case KEY_SHIFTNUMPAD4:
		case KEY_SHIFTRIGHT:
		case KEY_SHIFTNUMPAD6:
		case KEY_SHIFTUP:
		case KEY_SHIFTNUMPAD8:
		case KEY_SHIFTDOWN:
		case KEY_SHIFTNUMPAD2:
		case KEY_SHIFTPGUP:
		case KEY_SHIFTNUMPAD9:
		case KEY_SHIFTPGDN:
		case KEY_SHIFTNUMPAD3:
		case KEY_CTRLSHIFTLEFT:
		case KEY_CTRLSHIFTNUMPAD4:
		case KEY_CTRLSHIFTRIGHT:
		case KEY_CTRLSHIFTNUMPAD6:
			return true;
		default:
			return false;
	}
}

bool IsEditorCopyKey(FarKey Key)
{
	switch (Key) {
		case KEY_CTRLC:
		case KEY_CTRLINS:
		case KEY_CTRLNUMPAD0:
		case KEY_CTRLADD:
			return true;
		default:
			return false;
	}
}

class DiffEditorPane
{
	ScreenObject *m_owner = nullptr;
	FARString m_path;
	UINT m_codepage = CP_AUTODETECT;
	std::vector<FARString> m_lines;
	std::unique_ptr<Editor> m_editor;
	bool m_colorerOpened = false;
	int m_syncedTopLine = -1;
	int m_syncedTopVisualLine = -1;
	int m_x1 = -1;
	int m_y1 = -1;
	int m_x2 = -1;
	int m_y2 = -1;

	class PluginEditorScope
	{
		Editor *m_prev = nullptr;

	public:
		PluginEditorScope(Editor *EditorPtr)
		{
			if (CtrlObject) {
				m_prev = CtrlObject->Plugins.CurDialogEditor;
				CtrlObject->Plugins.CurDialogEditor = EditorPtr;
			}
		}

		~PluginEditorScope()
		{
			if (CtrlObject)
				CtrlObject->Plugins.CurDialogEditor = m_prev;
		}
	};

public:
	DiffEditorPane(ScreenObject *Owner, const FARString &Path)
		: m_owner(Owner), m_path(Path)
	{
	}

	~DiffEditorPane()
	{
		CloseColorer();
	}

	bool Load()
	{
		m_editor.reset(new (std::nothrow) Editor(m_owner, true));
		if (!m_editor)
			return false;

		m_editor->SetVirtualFileName(m_path.CPtr());

		std::vector<FARString> EditorLines;
		if (!LoadTextFile(m_path, m_lines, &EditorLines, &m_codepage))
			return false;

		m_editor->FreeAllocatedData(false);
		m_editor->SetCodePage(m_codepage);
		m_editor->BeginBulkLoad();

		if (EditorLines.empty()) {
			m_editor->InsertString(L"", 0);
		} else {
			for (const auto &Line : EditorLines)
				m_editor->InsertString(Line.CPtr(), static_cast<int>(Line.GetLength()));
		}

		m_editor->EndBulkLoad();
		m_editor->SetReadOnly(TRUE);
		m_editor->SetShowCursor(false);
		m_editor->SetShowScrollBar(FALSE);
		m_editor->SetShowLineNumbers(TRUE);
		m_editor->SetWordWrap(TRUE);
		return true;
	}

	void SetPosition(int X1, int Y1, int X2, int Y2)
	{
		if (X2 < X1)
			X2 = X1;
		const bool Changed = X1 != m_x1 || Y1 != m_y1 || X2 != m_x2 || Y2 != m_y2;
		m_x1 = X1;
		m_y1 = Y1;
		m_x2 = X2;
		m_y2 = Y2;
		if (m_editor) {
			m_editor->SetPosition(X1, Y1, X2, Y2);
			if (Changed)
				UpdateColorer();
		}
	}

	const FARString &Path() const { return m_path; }
	const std::vector<FARString> &Lines() const { return m_lines; }
	Editor *GetEditor() const { return m_editor.get(); }
	void SetActive(bool Active)
	{
		if (m_editor)
			m_editor->SetShowCursor(Active);
	}
	int VisualLineCount(int Line) const
	{
		return m_editor && Line >= 0 ? m_editor->GetVisualLineCount(Line) : 1;
	}
	int VisualOffset(int FirstLine, int FirstVisualLine, int Line, int VisualLine) const
	{
		if (!m_editor || FirstLine < 0 || FirstVisualLine < 0 || Line < FirstLine || VisualLine < 0)
			return -1;

		int Offset = -FirstVisualLine;
		for (int I = FirstLine; I < Line; ++I)
			Offset+= VisualLineCount(I);
		return Offset + VisualLine;
	}
	bool RenderLine(int Line, int VisualLine, int X1, int Y, int X2) const
	{
		return m_editor && Line >= 0 && m_editor->RenderVisualLine(Line, VisualLine, X1, Y, X2);
	}
	void ApplyInlineHighlight(int Line, int VisualLine, int DrawX1, int DrawX2, int DrawY,
			const std::vector<InlineRange> &Ranges, DiffKind Kind) const
	{
		if (!m_editor || Line < 0 || Ranges.empty())
			return;

		for (const InlineRange &Range : Ranges) {
			int CellX1 = 0;
			int CellX2 = 0;
			if (!m_editor->GetVisualLineHighlightCells(Line, VisualLine, Range.Start, Range.End,
						DrawX1, CellX1, CellX2))
				continue;

			CellX1 = std::max(CellX1, DrawX1);
			CellX2 = std::min(CellX2, DrawX2);
			ApplyInlineDiffOverlay(CellX1, CellX2, DrawY, Kind);
		}
	}
	void SyncViewport(int FirstVisibleLine, int FirstVisibleVisualLine)
	{
		if (!m_editor || FirstVisibleLine < 0 || FirstVisibleVisualLine < 0)
			return;

		const bool Changed = FirstVisibleLine != m_syncedTopLine || FirstVisibleVisualLine != m_syncedTopVisualLine;
		if (Changed) {
			m_editor->SetTopScreenLine(FirstVisibleLine, FirstVisibleVisualLine);
			m_syncedTopLine = FirstVisibleLine;
			m_syncedTopVisualLine = FirstVisibleVisualLine;
		}
		if (Changed || !m_colorerOpened)
			UpdateColorer();
	}
	bool ProcessKey(FarKey Key)
	{
		if (!m_editor)
			return false;

		PluginEditorScope Scope(m_editor.get());
		return m_editor->ProcessKey(Key) != 0;
	}
	void SetCursorByVisualLine(int Line, int VisualLine, int CellOffset)
	{
		if (!m_editor || Line < 0)
			return;

		m_editor->SetCursorByVisualLineCellOffset(Line, VisualLine, CellOffset);
	}
	bool ProcessMouse(const MOUSE_EVENT_RECORD &MouseEvent)
	{
		if (!m_editor)
			return false;

		PluginEditorScope Scope(m_editor.get());
		MOUSE_EVENT_RECORD Translated = MouseEvent;
		Translated.dwMousePosition.X = std::clamp<SHORT>(Translated.dwMousePosition.X, m_x1, m_x2);
		Translated.dwMousePosition.Y = std::clamp<SHORT>(Translated.dwMousePosition.Y, m_y1, m_y2);
		return m_editor->ProcessMouse(&Translated) != 0;
	}
	bool ProcessMouseAtLine(const MOUSE_EVENT_RECORD &MouseEvent, int Line, int VisualLine,
			int FirstVisibleLine, int FirstVisibleVisualLine)
	{
		if (!m_editor || Line < 0 || VisualLine < 0)
			return false;

		MOUSE_EVENT_RECORD Translated = MouseEvent;
		int Offset = VisualOffset(FirstVisibleLine, FirstVisibleVisualLine, Line, VisualLine);
		if (Offset < 0 || m_y1 + Offset > m_y2) {
			m_editor->SetTopScreenLine(Line, VisualLine);
			m_syncedTopLine = Line;
			m_syncedTopVisualLine = VisualLine;
			Offset = 0;
		}
		Translated.dwMousePosition.Y = static_cast<SHORT>(m_y1 + Offset);

		PluginEditorScope Scope(m_editor.get());
		return m_editor->ProcessMouse(&Translated) != 0;
	}
	int CursorLine() const { return m_editor ? m_editor->GetCursorLine() : 0; }
	int CursorVisualLine() const { return m_editor ? m_editor->GetCursorVisualLine() : 0; }

private:
	void UpdateColorer()
	{
		if (!CtrlObject || !m_editor)
			return;

		PluginEditorScope Scope(m_editor.get());
		if (!m_colorerOpened) {
			CtrlObject->Plugins.ProcessEditorEvent(EE_READ, nullptr);
			int EditorID = m_editor->GetEditorID();
			CtrlObject->Plugins.ProcessEditorEvent(EE_GOTFOCUS, &EditorID);
			m_colorerOpened = true;
		}
		CtrlObject->Plugins.ProcessEditorEvent(EE_REDRAW, EEREDRAW_ALL);
	}

	void CloseColorer()
	{
		if (!CtrlObject || !m_editor || !m_colorerOpened)
			return;

		PluginEditorScope Scope(m_editor.get());
		int EditorID = m_editor->GetEditorID();
		CtrlObject->Plugins.ProcessEditorEvent(EE_CLOSE, &EditorID);
		m_colorerOpened = false;
	}
};

class FileDiffFrame : public Frame
{
	enum class ActivePane
	{
		Left,
		Right
	};

	static constexpr int PreferredGutterWidth = 3;

	FARString m_leftPath;
	FARString m_rightPath;
	DiffEditorPane m_leftPane;
	DiffEditorPane m_rightPane;
	std::vector<DiffRow> m_rows;
	std::vector<DiffHunk> m_hunks;
	std::vector<InlineDiff> m_inlineDiffs;
	std::vector<ScreenRow> m_screenRows;
	size_t m_top = 0;
	int m_leftWidth = 0;
	int m_gutterWidth = 0;
	int m_rightWidth = 0;
	ActivePane m_activePane = ActivePane::Left;

public:
	FileDiffFrame(const FARString &LeftPath, const FARString &RightPath)
		: m_leftPath(LeftPath), m_rightPath(RightPath), m_leftPane(this, m_leftPath), m_rightPane(this, m_rightPath)
	{
		SetCanLoseFocus(TRUE);
		SetRestoreScreenMode(TRUE);
		KeyBarVisible = FALSE;
		SetPosition(0, 0, ScrX, ScrY);

		if (!m_leftPane.Load() || !m_rightPane.Load()) {
			SetExitCode(XC_OPEN_ERROR);
			Message(MSG_WARNING, 1, L"Compare files", L"Cannot open one of the selected files.", Msg::Ok);
			return;
		}
		UpdateActivePane();

		m_rows = BuildLineDiff(m_leftPane.Lines(), m_rightPane.Lines());
		BuildDiffHunks();
		BuildInlineDiffs();
		RebuildScreenRows();
		SetExitCode(TRUE);
		FrameManager->InsertFrame(this);
	}

	virtual const wchar_t *GetTypeName() { return L"[FileDiff]"; }
	virtual int GetType() { return MODALTYPE_USER; }
	virtual int GetTypeAndName(FARString &strType, FARString &strName)
	{
		strType = L"FileDiff";
		strName = m_leftPath;
		return MODALTYPE_USER;
	}

	virtual FARString &GetTitle(FARString &Title, int SubLen = -1, int TruncSize = 0)
	{
		Title = L"Compare files";
		return Title;
	}

	virtual void ResizeConsole()
	{
		SetPosition(0, 0, ScrX, ScrY);
		RebuildScreenRows();
	}

	virtual void OnChangeFocus(int focus)
	{
		Frame::OnChangeFocus(focus);
	}

	virtual int ProcessKey(FarKey Key)
	{
		switch (Key) {
			case KEY_ESC:
			case KEY_F10:
				FrameManager->DeleteFrame();
				return TRUE;
			case KEY_TAB:
			case KEY_SHIFTTAB:
				SwitchActivePane();
				return TRUE;
			case KEY_F12:
				return FrameManager->ProcessKey(KEY_F12);
			case KEY_MSWHEEL_UP:
				Scroll(-1);
				return TRUE;
			case KEY_MSWHEEL_DOWN:
				Scroll(1);
				return TRUE;
			case KEY_CTRLSHIFTUP:
			case KEY_CTRLSHIFTNUMPAD8:
				NavigateDiff(-1);
				return TRUE;
			case KEY_CTRLSHIFTDOWN:
			case KEY_CTRLSHIFTNUMPAD2:
				NavigateDiff(1);
				return TRUE;
		}
		if (IsEditorCursorKey(Key)) {
			ActiveEditorPane().ProcessKey(Key);
			EnsureActiveCursorVisible();
			Show();
			return TRUE;
		}
		if (IsEditorCopyKey(Key)) {
			ActiveEditorPane().ProcessKey(Key);
			Show();
			return TRUE;
		}
		return FALSE;
	}

	virtual int ProcessMouse(MOUSE_EVENT_RECORD *MouseEvent)
	{
		const int X = MouseEvent->dwMousePosition.X;
		const int Y = MouseEvent->dwMousePosition.Y;

		if (Y >= Y1 + 1 && Y <= Y2 - 1 && !MouseInsideGutter(X)) {
			const ActivePane NewPane = X >= RightPaneX1() ? ActivePane::Right : ActivePane::Left;
			if (NewPane != m_activePane) {
				const size_t CursorIndex = ActiveCursorScreenIndex();
				m_activePane = NewPane;
				SyncActiveCursorToScreenIndex(CursorIndex);
				UpdateActivePane();
			}
		}

		if (MouseEvent->dwEventFlags == MOUSE_WHEELED) {
			const short Delta = HIWORD(MouseEvent->dwButtonState);
			Scroll(Delta > 0 ? -3 : 3);
			return TRUE;
		}

		if (!MouseInsideActivePane(*MouseEvent))
			return FALSE;

		if (ProcessPaneMouse(*MouseEvent)) {
			EnsureActiveCursorVisible();
			Show();
			return TRUE;
		}
		ActiveEditorPane().ProcessMouse(*MouseEvent);
		EnsureActiveCursorVisible();
		Show();
		return TRUE;
	}

private:
	size_t VisibleRows() const
	{
		return ObjHeight > 2 ? static_cast<size_t>(ObjHeight - 2) : 0;
	}

	size_t MaxTop() const
	{
		const size_t Page = VisibleRows();
		return m_screenRows.size() > Page ? m_screenRows.size() - Page : 0;
	}

	int GutterX1() const
	{
		return X1 + m_leftWidth;
	}

	int GutterX2() const
	{
		return GutterX1() + m_gutterWidth - 1;
	}

	int RightPaneX1() const
	{
		return GutterX2() + 1;
	}

	bool MouseInsideGutter(int X) const
	{
		return m_gutterWidth > 0 && X >= GutterX1() && X <= GutterX2();
	}

	void Scroll(int Delta)
	{
		const size_t OldTop = m_top;
		const size_t Page = VisibleRows();
		const size_t CursorIndex = ActiveCursorScreenIndex();
		const size_t CursorOffset = CursorIndex != InvalidIndex && CursorIndex >= OldTop && Page != 0
				? std::min(CursorIndex - OldTop, Page - 1)
				: 0;

		const int64_t NewTop = static_cast<int64_t>(m_top) + Delta;
		m_top = static_cast<size_t>(std::max<int64_t>(0, std::min<int64_t>(NewTop, static_cast<int64_t>(MaxTop()))));
		if (!m_screenRows.empty() && Page != 0 && m_top != OldTop)
			SyncActiveCursorToScreenIndex(std::min(m_top + CursorOffset, m_screenRows.size() - 1));
		Show();
	}

	void NavigateDiff(int Direction)
	{
		if (m_hunks.empty() || Direction == 0)
			return;

		const size_t TargetHunk = FindNavigationHunk(CurrentDiffRow(), Direction);
		if (TargetHunk == InvalidIndex)
			return;

		JumpToHunk(TargetHunk);
	}

	size_t FindNavigationHunk(size_t Row, int Direction) const
	{
		if (m_hunks.empty() || Row == InvalidIndex)
			return InvalidIndex;

		const size_t CurrentHunk = HunkIndexForRow(Row);
		if (Direction > 0) {
			if (CurrentHunk != InvalidIndex)
				return CurrentHunk + 1 < m_hunks.size() ? CurrentHunk + 1 : InvalidIndex;

			for (size_t I = 0; I < m_hunks.size(); ++I) {
				if (m_hunks[I].FirstRow > Row)
					return I;
			}
			return InvalidIndex;
		}

		if (CurrentHunk != InvalidIndex) {
			if (Row > m_hunks[CurrentHunk].FirstRow)
				return CurrentHunk;
			return CurrentHunk > 0 ? CurrentHunk - 1 : InvalidIndex;
		}

		for (size_t I = m_hunks.size(); I > 0; --I) {
			if (m_hunks[I - 1].FirstRow < Row)
				return I - 1;
		}
		return InvalidIndex;
	}

	void JumpToHunk(size_t HunkIndex)
	{
		if (HunkIndex >= m_hunks.size())
			return;

		const size_t RowIndex = m_hunks[HunkIndex].FirstRow;
		const size_t TargetScreenIndex = FirstScreenIndexForRow(RowIndex);
		if (TargetScreenIndex == InvalidIndex || RowIndex >= m_rows.size())
			return;

		SelectPaneForRow(m_rows[RowIndex]);
		m_top = std::min(TargetScreenIndex > 0 ? TargetScreenIndex - 1 : TargetScreenIndex, MaxTop());
		SyncActiveCursorToScreenIndex(TargetScreenIndex);
		UpdateActivePane();
		Show();
	}

	void SwitchActivePane()
	{
		const size_t CursorIndex = ActiveCursorScreenIndex();
		m_activePane = m_activePane == ActivePane::Left ? ActivePane::Right : ActivePane::Left;
		SyncActiveCursorToScreenIndex(CursorIndex);
		UpdateActivePane();
		Show();
	}

	void UpdateActivePane()
	{
		m_leftPane.SetActive(m_activePane == ActivePane::Left);
		m_rightPane.SetActive(m_activePane == ActivePane::Right);
	}

	DiffEditorPane &ActiveEditorPane()
	{
		return m_activePane == ActivePane::Left ? m_leftPane : m_rightPane;
	}

	const DiffEditorPane &ActiveEditorPane() const
	{
		return m_activePane == ActivePane::Left ? m_leftPane : m_rightPane;
	}

	void SelectPaneForRow(const DiffRow &Row)
	{
		if (m_activePane == ActivePane::Left && Row.Left < 0 && Row.Right >= 0)
			m_activePane = ActivePane::Right;
		else if (m_activePane == ActivePane::Right && Row.Right < 0 && Row.Left >= 0)
			m_activePane = ActivePane::Left;
		UpdateActivePane();
	}

	void GetFirstVisiblePositions(int &LeftLine, int &LeftVisualLine, int &RightLine, int &RightVisualLine) const
	{
		LeftLine = -1;
		LeftVisualLine = -1;
		RightLine = -1;
		RightVisualLine = -1;
		const size_t Page = VisibleRows();
		for (size_t I = 0; I < Page && m_top + I < m_screenRows.size(); ++I) {
			const ScreenRow &Screen = m_screenRows[m_top + I];
			const DiffRow &Row = m_rows[Screen.Row];
			if (LeftLine < 0 && Row.Left >= 0) {
				LeftLine = Row.Left;
				LeftVisualLine = static_cast<int>(Screen.Part);
			}
			if (RightLine < 0 && Row.Right >= 0) {
				RightLine = Row.Right;
				RightVisualLine = static_cast<int>(Screen.Part);
			}
			if (LeftLine >= 0 && RightLine >= 0)
				break;
		}
	}

	void FirstVisiblePosition(ActivePane Pane, int &Line, int &VisualLine) const
	{
		int LeftLine = -1;
		int LeftVisualLine = -1;
		int RightLine = -1;
		int RightVisualLine = -1;
		GetFirstVisiblePositions(LeftLine, LeftVisualLine, RightLine, RightVisualLine);
		Line = Pane == ActivePane::Left ? LeftLine : RightLine;
		VisualLine = Pane == ActivePane::Left ? LeftVisualLine : RightVisualLine;
	}

	bool ProcessPaneMouse(const MOUSE_EVENT_RECORD &MouseEvent)
	{
		const int Y = MouseEvent.dwMousePosition.Y;
		if (Y < Y1 + 1 || Y > Y2 - 1)
			return false;

		const size_t ScreenIndex = m_top + static_cast<size_t>(Y - (Y1 + 1));
		if (ScreenIndex >= m_screenRows.size())
			return false;

		const ScreenRow &Screen = m_screenRows[ScreenIndex];
		const DiffRow &Row = m_rows[Screen.Row];
		const int Line = m_activePane == ActivePane::Left ? Row.Left : Row.Right;
		if (Line < 0)
			return false;

		int FirstLine = -1;
		int FirstVisualLine = -1;
		FirstVisiblePosition(m_activePane, FirstLine, FirstVisualLine);
		return ActiveEditorPane().ProcessMouseAtLine(MouseEvent, Line, static_cast<int>(Screen.Part),
				FirstLine, FirstVisualLine);
	}

	bool MouseInsideActivePane(const MOUSE_EVENT_RECORD &MouseEvent) const
	{
		const int X = MouseEvent.dwMousePosition.X;
		const int Y = MouseEvent.dwMousePosition.Y;
		if (Y < Y1 + 1 || Y > Y2 - 1)
			return false;
		return m_activePane == ActivePane::Left ? X >= X1 && X < GutterX1() : X >= RightPaneX1() && X <= X2;
	}

	void EnsureActiveCursorVisible()
	{
		const size_t Candidate = ActiveCursorScreenIndex();

		if (Candidate == InvalidIndex)
			return;

		const size_t Page = VisibleRows();
		if (Candidate < m_top)
			m_top = Candidate;
		else if (Page != 0 && Candidate >= m_top + Page)
			m_top = Candidate - Page + 1;
		m_top = std::min(m_top, MaxTop());
	}

	void SyncActiveCursorToScreenIndex(size_t ScreenIndex)
	{
		if (ScreenIndex == InvalidIndex || ScreenIndex >= m_screenRows.size())
			return;

		ScreenRow Screen = m_screenRows[ScreenIndex];
		DiffRow Row = m_rows[Screen.Row];
		int Line = m_activePane == ActivePane::Left ? Row.Left : Row.Right;

		if (Line < 0) {
			const size_t Page = VisibleRows();
			const size_t ViewEnd = std::min(m_top + Page, m_screenRows.size());
			for (int Pass = 0; Line < 0 && Pass < 2; ++Pass) {
				for (size_t Distance = 1; Distance < m_screenRows.size(); ++Distance) {
					bool Found = false;
					for (const int Direction : {-1, 1}) {
						const int64_t CandidateIndex = static_cast<int64_t>(ScreenIndex)
								+ static_cast<int64_t>(Direction) * static_cast<int64_t>(Distance);
						if (CandidateIndex < 0 || CandidateIndex >= static_cast<int64_t>(m_screenRows.size()))
							continue;
						if (Pass == 0
								&& (Page == 0 || static_cast<size_t>(CandidateIndex) < m_top
										|| static_cast<size_t>(CandidateIndex) >= ViewEnd)) {
							continue;
						}

						Screen = m_screenRows[CandidateIndex];
						Row = m_rows[Screen.Row];
						Line = m_activePane == ActivePane::Left ? Row.Left : Row.Right;
						if (Line >= 0) {
							Found = true;
							break;
						}
					}
					if (Found)
						break;
				}
			}
		}

		if (Line < 0)
			return;

		const int VisualLine = std::min(static_cast<int>(Screen.Part), ActiveEditorPane().VisualLineCount(Line) - 1);
		ActiveEditorPane().SetCursorByVisualLine(Line, VisualLine, 0);
		EnsureActiveCursorVisible();
	}

	size_t ActiveCursorScreenIndex() const
	{
		const int CursorLine = ActiveEditorPane().CursorLine();
		const int CursorVisualLine = ActiveEditorPane().CursorVisualLine();
		size_t Candidate = InvalidIndex;

		for (size_t I = 0; I < m_screenRows.size(); ++I) {
			const ScreenRow &Screen = m_screenRows[I];
			const DiffRow &Row = m_rows[Screen.Row];
			const int Line = m_activePane == ActivePane::Left ? Row.Left : Row.Right;
			if (Line != CursorLine)
				continue;

			if (static_cast<int>(Screen.Part) == CursorVisualLine) {
				Candidate = I;
				break;
			}
			if (Candidate == InvalidIndex)
				Candidate = I;
		}

		return Candidate;
	}

	bool ActiveCursorVisible() const
	{
		const size_t CursorIndex = ActiveCursorScreenIndex();
		if (CursorIndex == InvalidIndex)
			return false;
		const size_t Page = VisibleRows();
		return CursorIndex >= m_top && CursorIndex < m_top + Page;
	}

	void UpdateCursorVisibilityForViewport()
	{
		const bool Visible = ActiveCursorVisible();
		m_leftPane.SetActive(Visible && m_activePane == ActivePane::Left);
		m_rightPane.SetActive(Visible && m_activePane == ActivePane::Right);
		if (!Visible)
			SetCursorType(FALSE, 0);
	}

	void RebuildScreenRows()
	{
		const int TotalWidth = std::max(1, ObjWidth);
		m_gutterWidth = TotalWidth >= 5 ? PreferredGutterWidth : TotalWidth >= 3 ? 1 : 0;
		const int PanesWidth = std::max(1, TotalWidth - m_gutterWidth);
		m_leftWidth = std::max(1, PanesWidth / 2);
		m_rightWidth = std::max(1, PanesWidth - m_leftWidth);
		m_leftPane.SetPosition(X1, Y1 + 1, X1 + m_leftWidth - 1, Y2 - 1);
		m_rightPane.SetPosition(RightPaneX1(), Y1 + 1, X2, Y2 - 1);

		m_screenRows.clear();
		for (size_t I = 0; I < m_rows.size(); ++I) {
			const DiffRow &Row = m_rows[I];
			const size_t Parts = std::max(static_cast<size_t>(m_leftPane.VisualLineCount(Row.Left)),
					static_cast<size_t>(m_rightPane.VisualLineCount(Row.Right)));
			for (size_t Part = 0; Part < Parts; ++Part)
				m_screenRows.push_back({I, Part});
		}
		if (m_screenRows.empty())
			m_screenRows.push_back({});
		m_top = std::min(m_top, MaxTop());
	}

	void BuildDiffHunks()
	{
		m_hunks.clear();
		for (size_t I = 0; I < m_rows.size();) {
			if (m_rows[I].Kind == DiffKind::Equal) {
				++I;
				continue;
			}

			const size_t FirstRow = I;
			while (I < m_rows.size() && m_rows[I].Kind != DiffKind::Equal)
				++I;
			m_hunks.push_back({FirstRow, I});
		}
	}

	void BuildInlineDiffs()
	{
		m_inlineDiffs.clear();
		m_inlineDiffs.resize(m_rows.size());

		for (size_t I = 0; I < m_rows.size(); ++I) {
			const DiffRow &Row = m_rows[I];
			if (Row.Kind != DiffKind::Changed || Row.Left < 0 || Row.Right < 0)
				continue;

			BuildInlineDiffRanges(m_leftPane.Lines()[Row.Left], m_rightPane.Lines()[Row.Right],
					m_inlineDiffs[I].Left, m_inlineDiffs[I].Right);
		}
	}

	size_t FirstScreenIndexForRow(size_t Row) const
	{
		for (size_t I = 0; I < m_screenRows.size(); ++I) {
			if (m_screenRows[I].Row == Row)
				return I;
		}
		return InvalidIndex;
	}

	size_t CurrentDiffRow() const
	{
		const size_t CursorIndex = ActiveCursorScreenIndex();
		if (CursorIndex != InvalidIndex && CursorIndex < m_screenRows.size())
			return m_screenRows[CursorIndex].Row;
		if (m_top < m_screenRows.size())
			return m_screenRows[m_top].Row;
		return InvalidIndex;
	}

	size_t HunkIndexForRow(size_t Row) const
	{
		for (size_t I = 0; I < m_hunks.size(); ++I) {
			if (Row >= m_hunks[I].FirstRow && Row < m_hunks[I].LastRow)
				return I;
		}
		return InvalidIndex;
	}

	size_t VisibleHunkActionScreenIndex(size_t HunkIndex) const
	{
		if (HunkIndex >= m_hunks.size())
			return InvalidIndex;

		const DiffHunk &Hunk = m_hunks[HunkIndex];
		const size_t ViewEnd = std::min(m_top + VisibleRows(), m_screenRows.size());
		size_t First = InvalidIndex;
		size_t Last = InvalidIndex;
		for (size_t I = m_top; I < ViewEnd; ++I) {
			const size_t Row = m_screenRows[I].Row;
			if (Row < Hunk.FirstRow)
				continue;
			if (Row >= Hunk.LastRow) {
				if (First != InvalidIndex)
					break;
				continue;
			}

			if (First == InvalidIndex)
				First = I;
			Last = I;
		}

		return First == InvalidIndex ? InvalidIndex : First + (Last - First) / 2;
	}

	void DrawHeader()
	{
		SetScreen(X1, Y1, X2, Y1, L' ', FarColorToReal(COL_VIEWERSTATUS));
		DrawHeaderSide(X1, m_leftWidth, m_leftPath);
		DrawGutterHeader();
		DrawHeaderSide(RightPaneX1(), m_rightWidth, m_rightPath);
	}

	void DrawHeaderSide(int X, int Width, FARString Path)
	{
		if (Width <= 0)
			return;
		TruncPathStr(Path, Width);
		Text(X, Y1, FarColorToReal(COL_VIEWERSTATUS), Path.CPtr(), Path.GetLength());
	}

	void DrawGutterHeader()
	{
		if (m_gutterWidth <= 0)
			return;

		SetScreen(GutterX1(), Y1, GutterX2(), Y1, L' ', FarColorToReal(COL_VIEWERSTATUS));
		if (m_gutterWidth >= 3) {
			const wchar_t Gutter[] = {L' ', BoxSymbols[BS_V1], L' '};
			Text(GutterX1(), Y1, FarColorToReal(COL_VIEWERSTATUS), Gutter, ARRAYSIZE(Gutter));
		} else {
			Text(GutterX1(), Y1, FarColorToReal(COL_VIEWERSTATUS), &BoxSymbols[BS_V1], 1);
		}
	}

	void DrawStatus()
	{
		const int StatusWidth = X2 - X1 + 1;
		if (StatusWidth == 0)
			return;

		SetFarColor(COL_VIEWERSTATUS);
		GotoXY(X1, Y2);
		FS << fmt::Cells() << fmt::LeftAlign() << fmt::Size(StatusWidth) << L"";

		FARString HunkStatus;
		const size_t CurrentHunk = HunkIndexForRow(CurrentDiffRow());
		if (CurrentHunk != InvalidIndex) {
			HunkStatus.Format(L"Hunk: %u/%u", static_cast<unsigned>(CurrentHunk + 1),
					static_cast<unsigned>(m_hunks.size()));
		} else {
			HunkStatus.Format(L"Hunks: %u", static_cast<unsigned>(m_hunks.size()));
		}

		FARString Help = L"Esc/F10 Close  Ctrl+Shift+Up/Down Diff  Tab Switch pane";

		FormatString Info;
		Info << L"Active: " << (m_activePane == ActivePane::Left ? L"left" : L"right") << L"  " << HunkStatus
				<< L"  Lines: " << static_cast<UINT64>(m_leftPane.Lines().size()) << L'/'
				<< static_cast<UINT64>(m_rightPane.Lines().size())
				<< L"  Diff rows: " << static_cast<UINT64>(m_rows.size());

		const int InfoWidth = std::min(StatusWidth, static_cast<int>(Info.strValue().CellsCount()));
		const int HelpWidth = InfoWidth == StatusWidth ? 0 : StatusWidth - InfoWidth - 1;
		if (HelpWidth > 0) {
			GotoXY(X1, Y2);
			FS << fmt::Cells() << fmt::LeftAlign() << fmt::Size(HelpWidth) << Help;
		}

		if (InfoWidth > 0) {
			GotoXY(X2 - InfoWidth + 1, Y2);
			FS << fmt::Cells() << fmt::LeftAlign() << fmt::Size(InfoWidth) << Info.strValue();
		}
	}

	void DrawPane(int X, int Y, int Width, const DiffEditorPane &Pane, int LineIndex, DiffKind Kind,
			size_t Part, const std::vector<InlineRange> &InlineRanges, DiffKind InlineKind)
	{
		const uint64_t Color = FarColorToReal(COL_VIEWERTEXT);
		SetScreen(X, Y, X + Width - 1, Y, L' ', Color);

		if (!Pane.RenderLine(LineIndex, static_cast<int>(Part), X, Y, X + Width - 1))
			SetScreen(X, Y, X + Width - 1, Y, L' ', Color);
		ApplyDiffOverlay(X, Y, Width, Kind);
		Pane.ApplyInlineHighlight(LineIndex, static_cast<int>(Part), X, X + Width - 1, Y, InlineRanges, InlineKind);
	}

	void DrawGutter(int Y, size_t ScreenIndex, DiffKind Kind)
	{
		if (m_gutterWidth <= 0)
			return;

		const uint64_t Color = FarColorToReal(COL_VIEWERTEXT);
		SetScreen(GutterX1(), Y, GutterX2(), Y, L' ', Color);
		if (m_gutterWidth >= 3) {
			wchar_t Gutter[] = {L' ', BoxSymbols[BS_V1], L' '};
			const ScreenRow &Screen = m_screenRows[ScreenIndex];
			const bool DrawAction = ScreenIndex == VisibleHunkActionScreenIndex(HunkIndexForRow(Screen.Row));
			if (DrawAction) {
				switch (Kind) {
					case DiffKind::Deleted:
						Gutter[2] = L'\x25B8';
						break;
					case DiffKind::Added:
						Gutter[0] = L'\x25C2';
						break;
					case DiffKind::Changed:
						Gutter[0] = L'\x25C2';
						Gutter[2] = L'\x25B8';
						break;
					case DiffKind::Equal:
					default:
						break;
				}
			}
			Text(GutterX1(), Y, Color, Gutter, ARRAYSIZE(Gutter));
		} else {
			Text(GutterX1(), Y, Color, &BoxSymbols[BS_V1], 1);
		}
		ApplyDiffOverlay(GutterX1(), Y, m_gutterWidth, Kind);
	}

	virtual void DisplayObject()
	{
		RebuildScreenRows();
		SyncPaneViewports();
		UpdateCursorVisibilityForViewport();
		DrawHeader();

		const size_t Page = VisibleRows();
		for (size_t I = 0; I < Page; ++I) {
			const int Y = Y1 + 1 + static_cast<int>(I);
			const size_t ScreenIndex = m_top + I;
			if (ScreenIndex >= m_screenRows.size()) {
				SetScreen(X1, Y, X2, Y, L' ', FarColorToReal(COL_VIEWERTEXT));
				continue;
			}

			const ScreenRow &Screen = m_screenRows[ScreenIndex];
			const DiffRow &Row = m_rows[Screen.Row];
			const InlineDiff &Inline = m_inlineDiffs[Screen.Row];
			DrawPane(X1, Y, m_leftWidth, m_leftPane, Row.Left, Row.Kind, Screen.Part,
					Inline.Left, DiffKind::Deleted);
			DrawGutter(Y, ScreenIndex, Row.Kind);
			DrawPane(RightPaneX1(), Y, m_rightWidth, m_rightPane, Row.Right, Row.Kind, Screen.Part,
					Inline.Right, DiffKind::Added);
		}

		DrawStatus();
	}

	void SyncPaneViewports()
	{
		int LeftLine = -1;
		int LeftVisualLine = -1;
		int RightLine = -1;
		int RightVisualLine = -1;
		GetFirstVisiblePositions(LeftLine, LeftVisualLine, RightLine, RightVisualLine);
		m_leftPane.SyncViewport(LeftLine, LeftVisualLine);
		m_rightPane.SyncViewport(RightLine, RightVisualLine);
	}
};
}

void PresentFileDiff()
{
	if (!CtrlObject || !CtrlObject->Cp())
		return;

	Panel *Active = CtrlObject->Cp()->ActivePanel;
	Panel *Passive = CtrlObject->Cp()->GetAnotherPanel(Active);

	FARString LeftPath, RightPath;
	if (!ResolvePanelFile(Active, LeftPath) || !ResolvePanelFile(Passive, RightPath)) {
		Message(MSG_WARNING, 1, L"Compare files", L"Select regular local files on both file panels.", Msg::Ok);
		return;
	}

	FileDiffFrame *Diff = new (std::nothrow) FileDiffFrame(LeftPath, RightPath);
	if (!Diff) {
		Message(MSG_WARNING, 1, L"Compare files", L"Cannot allocate compare view.", Msg::Ok);
		return;
	}
	if (Diff->GetExitCode() == XC_OPEN_ERROR)
		delete Diff;
}
