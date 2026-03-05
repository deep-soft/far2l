#include "Common.h"
#include "Settings.h"
#include <KeyFileHelper.h>
#include <utils.h>
#include <wchar.h>

#define DEFAULT_IMAGE_MASKS "*.jpg *.jpeg *.png *.gif *.webp *.heic *.heif *.tiff *.tif *.bmp"
#define INI_PATH "plugins/preview/config.ini"
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
#define INI_ENABLED "Enabled"
#define INI_NATIVE_IMPL "NativeImplementation"

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
		_enabled = sv->GetInt(INI_ENABLED, _enabled) != 0;
		_native_implementation = sv->GetInt(INI_NATIVE_IMPL, _native_implementation) != 0;

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
	const wchar_t *msg = (g_far.GetMsg) ? g_far.GetMsg(g_far.ModuleNumber, msgId) : nullptr;
	if (msg && *msg) {
		return msg;
	}
	static const wchar_t *default_msgs[] = {
		L"Preview",            // M_TITLE
		L"&OK",                 // M_OK
		L"&Cancel",             // M_CANCEL
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
		L"Use EXIF &orientation",
		L"&Open by Enter",
		L"Open by &Ctrl+PgDn",
		L"Open in &QuickView",
		L"Override &F3 viewer",
		L"&Auto-fit after rotation",
		L"&Fast transforms",
		L"Compact &frame",
		L"Image masks",
		L"&Enable Preview",
		L"Use &OS engine",
		L"",
		L"",
	};
	if (msgId >= 0 && msgId < (int)(sizeof(default_msgs)/sizeof(default_msgs[0]))) {
		return default_msgs[msgId];
	}
	return L"";
}

enum DialogItems {
	DI_CFG_DOUBLEBOX = 0,
	DI_CFG_ENABLEPLUGIN,
	DI_CFG_USEORIENTATION,
	DI_CFG_OPENBYENTER,
	DI_CFG_OPENBYCTRLPGDN,
	DI_CFG_OPENINQVIEW,
	DI_CFG_OPENINFVIEW,
	DI_CFG_AUTOFITONROTATE,
	DI_CFG_FASTTRANSFORMS,
	DI_CFG_COMPACTFRAME,
	DI_CFG_IMPLEMENTATION,
	DI_CFG_SEPARATOR,
	DI_CFG_IMAGEMASKS_LABEL,
	DI_CFG_IMAGEMASKS_EDIT,
	DI_CFG_OK,
	DI_CFG_CANCEL,
	DI_CFG_COUNT
};

void Settings::ConfigurationDialog()
{
	const int w = 45, h = 19;

	struct FarDialogItem fdi[DI_CFG_COUNT] = {
	{DI_DOUBLEBOX, 3, 1, w - 4, h - 2, 0, {}, 0, 0, Msg(M_TITLE), 0},
	{DI_CHECKBOX, 5, 2, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_ENABLEPLUGIN), 0},
	{DI_CHECKBOX, 5, 3, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_USEORIENTATION), 0},
	{DI_CHECKBOX, 5, 4, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENBYENTER), 0},
	{DI_CHECKBOX, 5, 5, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENBYCTRLPGDN), 0},
	{DI_CHECKBOX, 5, 6, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENINQVIEW), 0},
	{DI_CHECKBOX, 5, 7, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_OPENINFVIEW), 0},
	{DI_CHECKBOX, 5, 8, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_AUTOFITONROTATE), 0},
	{DI_CHECKBOX, 5, 9, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_FASTTRANSFORMS), 0},
	{DI_CHECKBOX, 5, 10, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_COMPACTFRAME), 0},
	{DI_CHECKBOX, 5, 11, 0, 0, TRUE, {}, 0, 0, Msg(M_TEXT_IMPLEMENTATION), 0},
	{DI_SINGLEBOX, 4, 12, w - 5, 0, 0, {}, DIF_BOXCOLOR|DIF_SEPARATOR, 0, nullptr, 0},
	{DI_TEXT, 5, 13, w - 11, 0, FALSE, {}, 0, 0, Msg(M_TEXT_IMAGEMASKS), 0},
	{DI_EDIT, 5, 14, w - 6, 0, 0, {}, 0, 0, nullptr, 0},
	{DI_BUTTON, 0, 16, 0, 0, FALSE, {}, DIF_CENTERGROUP, TRUE, Msg(M_OK), 0},
	{DI_BUTTON, 0, 16, 0, 0, FALSE, {}, DIF_CENTERGROUP, 0, Msg(M_CANCEL), 0}
	};

	fdi[DI_CFG_ENABLEPLUGIN].Param.Selected = _enabled;
	fdi[DI_CFG_USEORIENTATION].Param.Selected = _use_orientation;
	fdi[DI_CFG_OPENBYENTER].Param.Selected = _open_by_enter;
	fdi[DI_CFG_OPENBYCTRLPGDN].Param.Selected = _open_by_cpgdn;
	fdi[DI_CFG_OPENINQVIEW].Param.Selected = _open_in_qv;
	fdi[DI_CFG_OPENINFVIEW].Param.Selected = _open_in_fv;
	fdi[DI_CFG_AUTOFITONROTATE].Param.Selected = _autofit_on_rotate;
	fdi[DI_CFG_FASTTRANSFORMS].Param.Selected = _fast_transforms;
	fdi[DI_CFG_COMPACTFRAME].Param.Selected = _compact_frame;
	fdi[DI_CFG_IMPLEMENTATION].Param.Selected = _native_implementation;
	
	std::wstring image_masks;
	StrMB2Wide(_image_masks, image_masks);
	fdi[DI_CFG_IMAGEMASKS_EDIT].PtrData = image_masks.c_str();

	auto dlg = g_far.DialogInit(g_far.ModuleNumber, -1, -1, w, h, L"Preview", fdi, DI_CFG_COUNT, 0, 0, nullptr, 0);
	int r = g_far.DialogRun(dlg);

	if (r == DI_CFG_OK) {

		_enabled = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_ENABLEPLUGIN, 0) == BSTATE_CHECKED);
		_use_orientation = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_USEORIENTATION, 0) == BSTATE_CHECKED);
		_open_by_enter = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_OPENBYENTER, 0) == BSTATE_CHECKED);
		_open_by_cpgdn = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_OPENBYCTRLPGDN, 0) == BSTATE_CHECKED);
		_open_in_qv = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_OPENINQVIEW, 0) == BSTATE_CHECKED);
		_open_in_fv = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_OPENINFVIEW, 0) == BSTATE_CHECKED);
		_autofit_on_rotate = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_AUTOFITONROTATE, 0) == BSTATE_CHECKED);
		_fast_transforms = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_FASTTRANSFORMS, 0) == BSTATE_CHECKED);
		_compact_frame = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_COMPACTFRAME, 0) == BSTATE_CHECKED);
		_native_implementation = (g_far.SendDlgMessage(dlg, DM_GETCHECK, DI_CFG_IMPLEMENTATION, 0) == BSTATE_CHECKED);
		
		image_masks = (const wchar_t *)g_far.SendDlgMessage(dlg, DM_GETCONSTTEXTPTR, DI_CFG_IMAGEMASKS_EDIT, 0);
		StrWide2MB(image_masks, _image_masks);

		KeyFileHelper kfh(_ini_path);
		kfh.SetInt(INI_SETTINGS, INI_ENABLED, _enabled);
		kfh.SetInt(INI_SETTINGS, INI_USEORIENTATION, _use_orientation);
		kfh.SetInt(INI_SETTINGS, INI_OPENBYENTER, _open_by_enter);
		kfh.SetInt(INI_SETTINGS, INI_OPENBYCPGDN, _open_by_cpgdn);
		kfh.SetInt(INI_SETTINGS, INI_OPENINQV, _open_in_qv);
		kfh.SetInt(INI_SETTINGS, INI_OPENINFV, _open_in_fv);
		kfh.SetInt(INI_SETTINGS, INI_AUTOFITONROTATE, _autofit_on_rotate);
		kfh.SetInt(INI_SETTINGS, INI_FASTTRANSFORMS, _fast_transforms);
		kfh.SetInt(INI_SETTINGS, INI_COMPACTFRAME, _compact_frame);
		kfh.SetInt(INI_SETTINGS, INI_NATIVE_IMPL, _native_implementation);

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
