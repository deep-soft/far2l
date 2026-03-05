#include "Common.h"
#include "ImageView.h"
#include "Settings.h"
#include "PreviewLog.h"

PluginStartupInfo g_far;
FarStandardFunctions g_fsf;

void PurgeAccumulatedInputEvents()
{
	WINPORT(CheckForKeyPress)(NULL, NULL, 0, CFKP_KEEP_OTHER_EVENTS);
}

bool CheckForEscAndPurgeAccumulatedInputEvents()
{
	WORD keys[] = {VK_ESCAPE};
	if (WINPORT(CheckForKeyPress)(NULL, keys, ARRAYSIZE(keys),
		CFKP_KEEP_MATCHED_KEY_EVENTS | CFKP_KEEP_UNMATCHED_KEY_EVENTS | CFKP_KEEP_MOUSE_EVENTS | CFKP_KEEP_OTHER_EVENTS) == 0) {
		return false;
	}
	PurgeAccumulatedInputEvents();
	return true;
}

void RectReduce(SMALL_RECT &rc)
{
	if (rc.Right - rc.Left >= 2) {
		rc.Left++;
		rc.Right--;
	}
	if (rc.Bottom - rc.Top >= 2) {
		rc.Top++;
		rc.Bottom--;
	}
}

SHAREDSYMBOL void WINAPI SetStartupInfoW(const struct PluginStartupInfo *Info)
{
	g_far = *Info;
	g_fsf = *Info->FSF;
}

SHAREDSYMBOL void WINAPI GetPluginInfoW(struct PluginInfo *Info)
{
	Info->StructSize = sizeof(struct PluginInfo);
	Info->Flags = PF_VIEWER;

	static const wchar_t *menu_strings[1];
	menu_strings[0] = g_settings.Msg(M_TITLE);

	Info->PluginConfigStrings = menu_strings;
	Info->PluginConfigStringsNumber = 1;
	Info->PluginMenuStrings = menu_strings;
	Info->PluginMenuStringsNumber = 1;

	Info->CommandPrefix = L"preview:";
}

static std::pair<std::string, bool> GetPanelItem(int cmd, int index)
{
	std::pair<std::string, bool> out;
	out.second = false;
	size_t sz = g_far.Control(PANEL_ACTIVE, cmd, index, 0);
	if (sz) {
		std::vector<char> buf(sz + 16);
		sz = g_far.Control(PANEL_ACTIVE, cmd, index, (LONG_PTR)buf.data());
		const PluginPanelItem *ppi = (const PluginPanelItem *)buf.data();
		if (ppi->FindData.lpwszFileName && *ppi->FindData.lpwszFileName) {
			out.first = Wide2MB(ppi->FindData.lpwszFileName);
			out.second = (ppi->Flags & PPIF_SELECTED) != 0;
		}
	}
	return out;
}

static std::string GetCurrentPanelItem()
{
	const auto &fn_sel = GetPanelItem(FCTL_GETCURRENTPANELITEM, 0);
	return fn_sel.first;
}

static ssize_t GetPanelItemsForView(const std::string &name, std::vector<std::pair<std::string, bool>> &all_files)
{
	PanelInfo pi{};
	g_far.Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi);
	if (pi.ItemsNumber <= 0) {
		return -1;
	}

	// Check if panel has real filesystem paths
	// Plugin panels (ADB, MTP, etc.) without PFLAGS_REALNAMES have virtual files
	if (!(pi.Flags & PFLAGS_REALNAMES)) {
		DBG("Panel doesn't have real names (PFLAGS_REALNAMES not set), skipping panel navigation");
		return -1;
	}

	bool has_real_selection = false;
	ssize_t cur = -1;
	const auto &cur_fn = GetCurrentPanelItem();
	for (int i = 0; i < pi.ItemsNumber; ++i) {
		const auto &fn_sel = GetPanelItem(FCTL_GETPANELITEM, i);
		if (!fn_sel.first.empty() && fn_sel.first != "." && fn_sel.first != "..") {
			if (fn_sel.second) {
				has_real_selection = true;
			}
			if (fn_sel.first == cur_fn) {
				cur = all_files.size();
			}
			all_files.emplace_back(fn_sel);
		}
	}

	std::vector<std::pair<std::string, bool>> chosen_files;
	const auto saved_cur = cur;
	for (ssize_t i = 0; i != (ssize_t)all_files.size(); ++i) {
		if (i == saved_cur) {
			cur = chosen_files.size();
			chosen_files.emplace_back(all_files[i]);
		} else {
			if (has_real_selection) {
				if (all_files[i].second) {
					chosen_files.emplace_back(all_files[i]);
				}
			} else if (g_settings.MatchFile(all_files[i].first.c_str())) {
				chosen_files.emplace_back(all_files[i]);
			}
		}
	}
	all_files = std::move(chosen_files);
	return all_files.empty() ? -1 : cur;
}

static EXITED_DUE OpenPluginAtCurrentPanel(const std::string &name)
{
	DBG("OpenPluginAtCurrentPanel name=%s", name.c_str());

	std::vector<std::pair<std::string, bool>> all_items;
	ssize_t initial_file = GetPanelItemsForView(name, all_items);

	DBG("GetPanelItemsForView returned %zd items, initial=%zd",
		all_items.size(), initial_file);

	if (initial_file < 0) {
		return EXITED_DUE_ERROR;
	}

	size_t final_file_index = initial_file;
	std::unordered_set<std::string> selection;
	auto ed = ShowImageAtFull(initial_file, all_items, selection, false, &final_file_index);

	std::string final_filename;
	if (final_file_index < all_items.size()) {
		final_filename = all_items[final_file_index].first;
	}

	PanelInfo pi{};
	g_far.Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi);
	if (pi.ItemsNumber > 0) {
		if (ed == EXITED_DUE_ENTER) {
			std::vector<bool> selection_to_apply;
			selection_to_apply.reserve(pi.ItemsNumber);
			for (int i = 0; i < pi.ItemsNumber; ++i) {
				const auto &fn_sel = GetPanelItem(FCTL_GETPANELITEM, i);
				selection_to_apply.emplace_back(selection.find(fn_sel.first) != selection.end());
			}
			g_far.Control(PANEL_ACTIVE, FCTL_BEGINSELECTION, 0, 0);
			for (size_t i = 0; i < selection_to_apply.size(); ++i) {
				BOOL selected = selection_to_apply[i] ? TRUE : FALSE;
				g_far.Control(PANEL_ACTIVE, FCTL_SETSELECTION, i, (LONG_PTR)selected);
			}
			g_far.Control(PANEL_ACTIVE, FCTL_ENDSELECTION, 0, 0);
		}

		PanelRedrawInfo ri = {};
		ri.CurrentItem = pi.CurrentItem;
		ri.TopPanelItem = pi.TopPanelItem;

		if (!final_filename.empty()) {
			for (int i = 0; i < pi.ItemsNumber; ++i) {
				const auto &fn_sel = GetPanelItem(FCTL_GETPANELITEM, i);
				if (fn_sel.first == final_filename) {
					ri.CurrentItem = i;
					ri.TopPanelItem = i;
					break;
				}
			}
		}

		g_far.Control(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, (LONG_PTR)&ri);
	}
	return ed;
}

static EXITED_DUE OpenPluginAtSomePath(const std::string &name, bool silent)
{
	return ShowImageAtFull(name, silent);
}

static void OpenAtCurrentPanelItem()
{
	const auto &fn_sel = GetCurrentPanelItem();
	if (!fn_sel.empty()) {
		struct PanelInfo pi{};
		g_far.Control(PANEL_PASSIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi);
		if (pi.Visible && pi.PanelType == PTYPE_QVIEWPANEL) {
			ShowImageAtQV(fn_sel,
				SMALL_RECT {
					SHORT(pi.PanelRect.left), SHORT(pi.PanelRect.top),
					SHORT(pi.PanelRect.right), SHORT(pi.PanelRect.bottom)
				}
			);
		} else {
			OpenPluginAtCurrentPanel(fn_sel);
		}
	}
}

SHAREDSYMBOL HANDLE WINAPI OpenPluginW(int OpenFrom, INT_PTR Item)
{
	if (!g_settings.Enabled()) {
		return INVALID_HANDLE_VALUE;
	}
	if (OpenFrom == OPEN_PLUGINSMENU) {
		OpenAtCurrentPanelItem();

	} else if (OpenFrom == OPEN_VIEWER) {
		ViewerInfo vi{sizeof(ViewerInfo), 0};
		if (g_far.ViewerControl(VCTL_GETINFO, &vi)) {
			if (vi.FileName && g_settings.MatchFile(Wide2MB(vi.FileName).c_str())) {
				if (OpenPluginAtSomePath(Wide2MB(vi.FileName), false) != EXITED_DUE_ERROR) {
				}
			}
		}

	} else if (Item > 0xfff) {
		std::string path = Wide2MB((const wchar_t *)Item);
		while (!path.empty() && path.front() == ' ') {
			path.erase(0, 1);
		}
		for (size_t i = 0; i + 1 < path.size(); ++i) {
			if (path[i] == '\\') {
				path.erase(i, 1);
			}
		}
		while (strncmp(path.c_str(), "./", 2) == 0) {
			path.erase(0, 2);
		}
		if (path.empty()) {
			OpenAtCurrentPanelItem();
		} else if (path.find('/') == std::string::npos) {
			OpenPluginAtCurrentPanel(path);
		} else {
			OpenPluginAtSomePath(path, false);
		}
	}

	return INVALID_HANDLE_VALUE;
}

SHAREDSYMBOL void WINAPI ExitFARW(void)
{
	DismissImageAtQV();
}

SHAREDSYMBOL void WINAPI ClosePluginW(HANDLE hPlugin) {}
SHAREDSYMBOL int WINAPI ProcessEventW(HANDLE hPlugin, int Event, void *Param) { return FALSE; }

static std::string g_last_intercepted_file;  // Prevent re-intercepting same file

SHAREDSYMBOL int WINAPI ProcessViewerEventW(int Event,void *Param)
{
	if (!g_settings.Enabled()) {
		return FALSE;
	}
	DBG("ProcessViewerEventW Event=%d", Event);
	if (Event == VE_READ || Event == VE_CLOSE) {
		// Get viewer info to check file name
		ViewerInfo vi{sizeof(ViewerInfo), 0};
		std::string viewer_file;
		if (g_far.ViewerControl(VCTL_GETINFO, &vi) && vi.FileName) {
			viewer_file = Wide2MB(vi.FileName);
			DBG("viewer_file='%s'", viewer_file.c_str());
		}

		struct PanelInfo pi{};
		g_far.Control(PANEL_PASSIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi);
		DBG("pi.Visible=%d pi.PanelType=%d OpenInQV=%d IsShowing=%d",
			pi.Visible, pi.PanelType, g_settings.OpenInQV(), IsShowingImageAtQV());

		// Handle QuickView panel
		if (pi.Visible && pi.PanelType == PTYPE_QVIEWPANEL && (IsShowingImageAtQV() || g_settings.OpenInQV())) {
			// Also get panel item for extension matching
			const auto &fn_sel = GetCurrentPanelItem();
			bool matches = g_settings.MatchFile(fn_sel.c_str());
			DBG("fn_sel='%s' match=%d", fn_sel.c_str(), matches ? 1 : 0);

			if (matches) {
				// File matches - show image
				std::string file_to_show = viewer_file.empty() ? fn_sel : viewer_file;
				if (!file_to_show.empty()) {
					DBG("calling ShowImageAtQV with '%s'", file_to_show.c_str());
					ShowImageAtQV(file_to_show,
						SMALL_RECT{
							SHORT(pi.PanelRect.left), SHORT(pi.PanelRect.top),
							SHORT(pi.PanelRect.right), SHORT(pi.PanelRect.bottom)
						}
					);
				}
			} else if (IsShowingImageAtQV()) {
				DBG("file doesn't match, calling DismissImageAtQV");
				DismissImageAtQV();
			}
		}
		// Handle full viewer (F3) - intercept for image files
		else if (Event == VE_READ && g_settings.OpenInFV() && !viewer_file.empty()) {
			// Check if the file being viewed is an image
			const wchar_t *slash = wcsrchr(vi.FileName, L'/');
			std::string filename_mb = Wide2MB(slash ? slash + 1 : vi.FileName);
			if (g_settings.MatchFile(filename_mb.c_str())) {
				// Prevent re-intercepting the same file
				if (g_last_intercepted_file != viewer_file) {
					g_last_intercepted_file = viewer_file;
					DBG("F3 viewer intercept: showing image '%s'", viewer_file.c_str());
					// Close internal viewer
					g_far.ViewerControl(VCTL_QUIT, nullptr);
					// Show our image viewer
					ShowImageAtFull(viewer_file, false);
					// After our viewer closes, post ESC to close any remaining internal viewer
					DWORD esc_key = KEY_ESC;
					KeySequence ks = {0, 1, &esc_key};
					g_far.AdvControl(g_far.ModuleNumber, ACTL_POSTKEYSEQUENCE, &ks, 0);
					DBG("F3 viewer: done, posted ESC");
					return TRUE;
				} else {
					DBG("F3 viewer: already intercepted this file, closing internal viewer");
					g_far.ViewerControl(VCTL_QUIT, nullptr);
					return TRUE;
				}
			}
		}
		// Dismiss QV image if panel changed
		else if (IsShowingImageAtQV()) {
			DBG("calling DismissImageAtQV");
			DismissImageAtQV();
		}
	}
	// Clear intercepted file on close
	if (Event == VE_CLOSE) {
		g_last_intercepted_file.clear();
	}
	return FALSE;
}

SHAREDSYMBOL int WINAPI ConfigureW(int ItemNumber)
{
	g_settings.ConfigurationDialog();
	return 1;
}

SHAREDSYMBOL HANDLE WINAPI _export OpenFilePluginW(const wchar_t *Name, const unsigned char *Data, int DataSize, int OpMode)
{
	if (!g_settings.Enabled()) {
		return INVALID_HANDLE_VALUE;
	}
	DBG("OpenFilePluginW Name=%ls OpMode=0x%x DataSize=%d",
		Name ? Name : L"(null)", OpMode, DataSize);

	if (Name) {
		// Name already contains full path - for plugin files, far2l extracts to temp
		const std::string path_mb = Wide2MB(Name);
		const wchar_t *slash = wcsrchr(Name, L'/');
		const std::string filename_mb = Wide2MB(slash ? slash + 1 : Name);

		DBG("path=%s filename=%s match=%d", path_mb.c_str(), filename_mb.c_str(),
			g_settings.MatchFile(filename_mb.c_str()) ? 1 : 0);

		if (g_settings.MatchFile(filename_mb.c_str())) {
			// Check if panel has real filesystem paths (for navigation support)
			PanelInfo pi{};
			g_far.Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi);
			bool has_real_paths = (pi.Flags & PFLAGS_REALNAMES) != 0;
			DBG("has_real_paths=%d", has_real_paths);

			// For real filesystem paths, use panel navigation
			// For plugin/temp paths, show single file
			auto show_image = [&]() -> EXITED_DUE {
				if (has_real_paths) {
					return OpenPluginAtCurrentPanel(filename_mb);
				} else {
					return ShowImageAtFull(path_mb, false);
				}
			};

			// Handle Enter
			if (OpMode == 0 && g_settings.OpenByEnter()) {
				DBG("handling Enter");
				EXITED_DUE result = show_image();
				DBG("show_image returned %d", (int)result);
				if (result != EXITED_DUE_ERROR) {
					return (HANDLE)-2;
				}
			}
			// Handle Ctrl+PgDn
			else if ((OpMode & OPM_PGDN) && g_settings.OpenByCtrlPgDn()) {
				DBG("handling Ctrl+PgDn");
				EXITED_DUE result = show_image();
				DBG("show_image returned %d", (int)result);
				if (result != EXITED_DUE_ERROR) {
					return (HANDLE)-2;
				}
			}
			// Handle F3 (view) - Override F3 viewer setting
			else if ((OpMode & OPM_VIEW) && g_settings.OpenInFV()) {
				DBG("handling F3");
				EXITED_DUE result = show_image();
				DBG("show_image returned %d", (int)result);
				if (result != EXITED_DUE_ERROR) {
					return (HANDLE)-2;
				}
			}
		}
	}

	return INVALID_HANDLE_VALUE;
}
