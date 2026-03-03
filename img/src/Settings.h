#ifndef FAR_SETTINGS_H
#define FAR_SETTINGS_H
#include <string>
#include <stdint.h>
#include "lng.h"

class Settings
{
public:
	enum DefaultScale {
		EQUAL_SCREEN = 0,
		LESSOREQUAL_SCREEN,
		EQUAL_IMAGE,

		INVALID_SCALE_EDGE_VALUE
	};

private:
	std::string _ini_path;
	DefaultScale _default_scale{EQUAL_SCREEN};
	bool _use_orientation = true;
	bool _open_by_enter = true;
	bool _open_by_cpgdn = true;
	bool _open_in_qv = true;
	bool _open_in_fv = true;
	bool _autofit_on_rotate = false;
	bool _fast_transforms = true;
	bool _compact_frame = false;
	std::string _image_masks;

public:
	Settings();
	const wchar_t *Msg(int msgId);
	void ConfigurationDialog();

	bool UseOrientation() const { return _use_orientation; }
	bool OpenByEnter() const { return _open_by_enter; }
	bool OpenByCtrlPgDn() const { return _open_by_cpgdn; }
	bool OpenInQV() const { return _open_in_qv; }
	bool OpenInFV() const { return _open_in_fv; }
	bool AutoFitOnRotate() const { return _autofit_on_rotate; }
	bool FastTransforms() const { return _fast_transforms; }
	bool CompactFrame() const { return _compact_frame; }

	DefaultScale GetDefaultScale() const { return _default_scale; }
	void SetDefaultScale(DefaultScale default_scale);

	bool MatchFile(const char *name) const;
};

extern Settings g_settings;

#endif
