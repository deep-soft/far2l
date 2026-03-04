#include "Common.h"
#include "Settings.h"
#include <KeyFileHelper.h>
#include <utils.h>
#include <wchar.h>

#define DEFAULT_IMAGE_MASKS "*.jpg *.jpeg *.png *.gif *.webp *.heic *.heif *.tiff *.tif *.bmp"
#define INI_PATH "plugins/img/config.ini"
#define INI_SETTINGS "Settings"
#define INI_DEFAULTSCALE "DefaultScale"
#define INI_USEORIENTATION "UseOrientation"
#define INI_OPENBYENTER "OpenByEnter"
#define INI_OPENBYCPGDN "OpenByCtrlPgDn"
#define INI_OPENINQV "OpenInQV"
#define INI_OPENINFV "OpenInFV"
#define INI_AUTOFITONROTATE "AutoFitOnRotate"
#define INI_FASTTRANSFORMS "FastTransforms"
#define INI_COMPACTFRAME "CompactFrame"
#define INI_IMAGEMASKS "ImageMasks"

Settings g_settings;

Settings::Settings()
{
	_ini_path = InMyConfig(INI_PATH);
	_image_masks = DEFAULT_IMAGE_MASKS;  // Set default first

	KeyFileReadHelper kfh(_ini_path);
	const KeyFileValues *sv = kfh.GetSectionValues(INI_SETTINGS);
	if (sv) {
		_use_orientation = sv->GetInt(INI_USEORIENTATION, _use_orientation) != 0;
		_open_by_enter = sv->GetInt(INI_OPENBYENTER, _open_by_enter) != 0;
		_open_by_cpgdn = sv->GetInt(INI_OPENBYCPGDN, _open_by_cpgdn) != 0;
		_open_in_qv = sv->GetInt(INI_OPENINQV, _open_in_qv) != 0;
		_open_in_fv = sv->GetInt(INI_OPENINFV, _open_in_fv) != 0;
		_autofit_on_rotate = sv->GetInt(INI_AUTOFITONROTATE, _autofit_on_rotate) != 0;
		_fast_transforms = sv->GetInt(INI_FASTTRANSFORMS, _fast_transforms) != 0;
		_compact_frame = sv->GetInt(INI_COMPACTFRAME, _compact_frame) != 0;

		std::string masks = sv->GetString(INI_IMAGEMASKS, "");
		if (!masks.empty()) {
			_image_masks = masks;
		}

		unsigned int default_scale = sv->GetUInt(INI_DEFAULTSCALE, _default_scale);
		if (default_scale < (unsigned int)INVALID_SCALE_EDGE_VALUE) {
			_default_scale = (DefaultScale)default_scale;
		}
	}
}

void Settings::SetDefaultScale(DefaultScale default_scale)
{
	_default_scale = default_scale;
	KeyFileHelper kfh(_ini_path);
	kfh.SetUInt(INI_SETTINGS, INI_DEFAULTSCALE, (unsigned int)_default_scale);
}

const wchar_t *Settings::Msg(int msgId)
{
	const wchar_t *msg = g_far.GetMsg(g_far.ModuleNumber, msgId);
	if (msg && *msg) {
		return msg;
	}
	static const wchar_t *default_msgs[] = {
		L"Image plugin",       // M_TITLE
		L"OK",                 // M_OK
		L"Cancel",             // M_CANCEL
		L"Extra commands",     // M_EXTRA_COMMANDS
		L"Extra commands",     // M_EXTRA_COMMANDS_TITLE
		L"Command name",       // M_INPUT_CMDNAME_TITLE
		L"Enter command name", // M_INPUT_CMDNAME_PROMPT
		L"Command line",       // M_INPUT_CMDLINE_TITLE
		L"Enter command line", // M_INPUT_CMDLINE_PROMPT
		L"PgUp/PgDn",          // M_HINT_NAVIGATE
		L"+/-/arrows",         // M_HINT_PAN
		L"Ins/Space/BS",       // M_HINT_SELECTION
		L"Enter/Esc/F1",       // M_HINT_OTHER
		L"Use EXIF orientation",
		L"Open by Enter",
		L"Open by Ctrl+PgDn",
		L"Open in QuickView",
		L"Override F3 viewer",
		L"Auto-fit after rotation",
		L"Fast transforms",
		L"Compact frame",
		L"Image masks",
	};
	if (msgId >= 0 && msgId < (int)(sizeof(default_msgs)/sizeof(default_msgs[0]))) {
		return default_msgs[msgId];
	}
	return L"";
}

void Settings::ConfigurationDialog()
{
	const int w = 50, h = 16;

	struct FarDialogItem fdi[] = {
	{DI_DOUBLEBOX, 1, 1, w - 2, h - 2, 0, {}, 0, 0, Msg(M_TITLE), 0},
	{DI_CHECKBOX, 3, 2, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_USEORIENTATION), 0},
	{DI_SINGLEBOX, 2, 3, w - 3, 0, 0, {}, DIF_BOXCOLOR|DIF_SEPARATOR, 0, nullptr, 0},
	{DI_CHECKBOX, 3, 4, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENBYENTER), 0},
	{DI_CHECKBOX, 3, 5, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENBYCTRLPGDN), 0},
	{DI_CHECKBOX, 3, 6, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENINQVIEW), 0},
	{DI_CHECKBOX, 3, 7, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENINFVIEW), 0},
	{DI_CHECKBOX, 3, 8, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_AUTOFITONROTATE), 0},
	{DI_CHECKBOX, 3, 9, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_FASTTRANSFORMS), 0},
	{DI_CHECKBOX, 3, 10, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_COMPACTFRAME), 0},
	{DI_TEXT, 3, 11, w - 9, 0, FALSE, {}, 0, 0, Msg(M_TEXT_IMAGEMASKS), 0},
	{DI_EDIT, 3, 12, w - 4, 0, 0, {}, 0, 0, nullptr, 0},
	{DI_SINGLEBOX, 2, 13, w - 3, 0, 0, {}, DIF_BOXCOLOR|DIF_SEPARATOR, 0, nullptr, 0},
	{DI_BUTTON, 27, 14, 0, 0, FALSE, {}, 0, TRUE, Msg(M_OK), 0},
	{DI_BUTTON, 35, 14, 0, 0, FALSE, {}, 0, 0, Msg(M_CANCEL), 0}
	};

	fdi[1].Param.Selected = _use_orientation;
	fdi[3].Param.Selected = _open_by_enter;
	fdi[4].Param.Selected = _open_by_cpgdn;
	fdi[5].Param.Selected = _open_in_qv;
	fdi[6].Param.Selected = _open_in_fv;
	fdi[7].Param.Selected = _autofit_on_rotate;
	fdi[8].Param.Selected = _fast_transforms;
	fdi[9].Param.Selected = _compact_frame;
	std::wstring image_masks;
	StrMB2Wide(_image_masks, image_masks);
	fdi[11].PtrData = image_masks.c_str();

	auto dlg = g_far.DialogInit(g_far.ModuleNumber, -1, -1, w, h, L"img", fdi, ARRAYSIZE(fdi), 0, 0, nullptr, 0);
	int r = g_far.DialogRun(dlg);

	if (r == ARRAYSIZE(fdi) - 2) {
		_use_orientation = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 1, 0) == BSTATE_CHECKED);
		_open_by_enter = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 3, 0) == BSTATE_CHECKED);
		_open_by_cpgdn = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 4, 0) == BSTATE_CHECKED);
		_open_in_qv = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 5, 0) == BSTATE_CHECKED);
		_open_in_fv = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 6, 0) == BSTATE_CHECKED);
		_autofit_on_rotate = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 7, 0) == BSTATE_CHECKED);
		_fast_transforms = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 8, 0) == BSTATE_CHECKED);
		_compact_frame = (g_far.SendDlgMessage(dlg, DM_GETCHECK, 9, 0) == BSTATE_CHECKED);
		image_masks = (const wchar_t *)g_far.SendDlgMessage(dlg, DM_GETCONSTTEXTPTR, 11, 0);
		StrWide2MB(image_masks, _image_masks);

		KeyFileHelper kfh(_ini_path);
		kfh.SetInt(INI_SETTINGS, INI_USEORIENTATION, _use_orientation);
		kfh.SetInt(INI_SETTINGS, INI_OPENBYENTER, _open_by_enter);
		kfh.SetInt(INI_SETTINGS, INI_OPENBYCPGDN, _open_by_cpgdn);
		kfh.SetInt(INI_SETTINGS, INI_OPENINQV, _open_in_qv);
		kfh.SetInt(INI_SETTINGS, INI_OPENINFV, _open_in_fv);
		kfh.SetInt(INI_SETTINGS, INI_AUTOFITONROTATE, _autofit_on_rotate);
		kfh.SetInt(INI_SETTINGS, INI_FASTTRANSFORMS, _fast_transforms);
		kfh.SetInt(INI_SETTINGS, INI_COMPACTFRAME, _compact_frame);

		if (_image_masks != DEFAULT_IMAGE_MASKS) {
			kfh.SetString(INI_SETTINGS, INI_IMAGEMASKS, _image_masks);
		} else {
			kfh.RemoveKey(INI_SETTINGS, INI_IMAGEMASKS);
		}
	}

	g_far.DialogFree(dlg);
}

static bool MatchAnyOfWildcardsICE(const char *name, const std::string &masks)
{
	const char *last_slash = strrchr(name, '/');
	if (last_slash && last_slash[1]) {
		name = last_slash + 1;
	}
	for (size_t i = 0, j = 0; i <= masks.size(); ++i) {
		if (i == masks.size() || masks[i] == ';' || masks[i] == ',' || masks[i] == ' ') {
			auto mask = masks.substr(j, i - j);
			StrTrim(mask);
			if (!mask.empty() && MatchWildcardICE(name, mask.c_str())) {
				return true;
			}
			j = i + 1;
		}
	}
	return false;
}

bool Settings::MatchFile(const char *name) const
{
	return MatchAnyOfWildcardsICE(name, _image_masks);
}
