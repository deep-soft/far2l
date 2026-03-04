#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <cstdio>
#include <unordered_set>
#include <signal.h>
#include <dirent.h>
#include <utils.h>
#include <math.h>
#include "Image.h"

class ImageView
{
	volatile bool *_cancel{nullptr};

	std::string _render_file, _tmp_file, _file_size_str;
	std::vector<std::pair<std::string, bool> > _all_files;
	size_t _initial_file{}, _cur_file{};
	WinportGraphicsInfo _wgi{};

	std::string _err_str;

	Image _orig_image, _scaled_image, _base_image, _ready_image, _tmp_image;
	double _scaled_image_scale{0}, _scale{-1}, _scale_fit{-1}, _scale_min{0.1}, _scale_max{4};
	COORD _pos{}, _size{};
	int _prev_left{0}, _prev_top{0};
	int _dx{0}, _dy{0};
	signed char _rotate{0}, _rotated{0};  // 90-degree rotation steps
	double _fine_rotate{0};  // Fine rotation in degrees (-45..45)
	bool _mirror_h{false}, _mirrored_h{false};
	bool _mirror_v{false}, _mirrored_v{false};
	bool _force_render{false};
	bool _base_dirty{true};  // Need to rebuild _base_image
	bool _fine_dirty{false}; // Fine rotation changed, need full re-render

	// Efficient decoding: track decoded size and original dimensions
	int _decoded_max_size{0};      // What maxPixelSize was used for current _orig_image
	int _original_width{0};        // Original image width (before any scaling)
	int _original_height{0};       // Original image height (before any scaling)
	bool _has_full_resolution{false};  // Do we have full resolution decoded?

	bool IterateFile(bool forward);
	bool PrepareImage();
	bool ReadImage();
	bool ReadImageInternal(int maxPixelSize);  // Internal: decode at specific size
	void ApplyEXIFOrientation(int orientation);
	bool EnsureFullResolution();  // Re-decode at full resolution if needed

	bool RefreshWGI();
	void SetupInitialScale(const int canvas_w, const int canvas_h);
	bool EnsureReadyAndScaled();
	uint16_t EnsureTransformed();

	bool SendWholeImage(const SMALL_RECT *area, const Image &img);
	bool SendWholeViewport(const SMALL_RECT *area, int src_left, int src_top, int viewport_w, int viewport_h);
	bool SendScrollAttachH(const SMALL_RECT *area, int src_left, int src_top, int viewport_w, int viewport_h, int delta);
	bool SendScrollAttachV(const SMALL_RECT *area, int src_left, int src_top, int viewport_w, int viewport_h, int delta);
	bool RenderImage();
	void DenoteState(const char *stage = NULL);
	void JustReset(bool keep_rotmir = false);

protected:
	virtual void DenoteInfoAndPan(const std::string &info, const std::string &pan);
	bool CurFileSelected() const { return _all_files[_cur_file].second; }

public:
	ImageView(size_t initial_file, const std::vector<std::pair<std::string, bool> > &all_files);
	virtual ~ImageView();

	const std::string &ErrorString() const { return _err_str; }
	const std::string &CurFile() const { return _all_files[_cur_file].first; }
	size_t GetCurrentFileIndex() const { return _cur_file; }
	std::unordered_set<std::string> GetSelection() const;

	bool Setup(SMALL_RECT &rc, volatile bool *cancel = nullptr);

	// Preload image for compact frame mode (returns false on error)
	bool Preload();

	// Get preferred dialog rectangle (for compact frame mode)
	SMALL_RECT GetPreferredRect(const SMALL_RECT &screen_rect) const;

	void Home();      // Go to first image
	void Last();      // Go to last image
	bool Iterate(bool forward);
	void Scale(int change);
	void Rotate(int change);       // 90-degree rotation
	void RotateFine(int degrees);  // Fine rotation (arbitrary angle)
	void Shift(int horizontal, int vertical);
	COORD ShiftByPixels(COORD delta);
	void MirrorH();
	void MirrorV();
	void Reset(bool keep_rotmir);
	void ForceShow()
	{
		_force_render = true;
		RenderImage();
		DenoteState();
	};
	void Select();
	void Deselect();
	void ToggleSelection();
};
