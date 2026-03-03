#include "Common.h"
#include "ImageView.h"
#include "Settings.h"
#include "ImgLog.h"
#include "decoder/ImageDecoder.h"

#define SETIMG_INITALLY_ASSUMED_SPEED    8192
#define SETIMG_DELAY_BASELINE_MSEC       256
#define SETIMG_ESTIMATION_SIZE_THRESHOLD 0x10000

bool ImageView::IterateFile(bool forward)
{
	if (forward) {
		++_cur_file;
		if (_cur_file >= _all_files.size()) {
			_cur_file = 0;
		}
	} else if (_cur_file > 0) {
		--_cur_file;
	} else {
		_cur_file = _all_files.size() - 1;
	}
	return true;
}

bool ImageView::PrepareImage()
{
	_render_file = CurFile();
	_orig_image.Resize();

	struct stat st {};
	if (stat(_render_file.c_str(), &st) == -1) {
		_all_files[_cur_file].second = false;
		return false;
	}
	if (!S_ISREG(st.st_mode) || st.st_size == 0) {
		_all_files[_cur_file].second = false;
		return false;
	}

	StrWide2MB(FileSizeString(st.st_size), _file_size_str);
	return ReadImage();
}

bool ImageView::ReadImage()
{
	return ReadImageInternal(0);  // 0 = auto (viewport size)
}

bool ImageView::ReadImageInternal(int maxPixelSize)
{
	const bool use_orientation = g_settings.UseOrientation();

	ImageDecoder* decoder = DecoderFactory::FindDecoder(_render_file);
	if (!decoder) {
		_err_str = "Unsupported image format";
		return false;
	}

	// If maxPixelSize is 0, decode at viewport size for efficiency
	if (maxPixelSize == 0 && _wgi.PixPerCell.X > 0 && _wgi.PixPerCell.Y > 0) {
		int viewport_w = _size.X * _wgi.PixPerCell.X;
		int viewport_h = _size.Y * _wgi.PixPerCell.Y;
		maxPixelSize = std::max(viewport_w, viewport_h);
	}

	int orientation = 1;
	if (!decoder->Decode(_render_file, _orig_image, orientation, maxPixelSize)) {
		_err_str = "Failed to decode image";
		return false;
	}

	// Track what we decoded
	_decoded_max_size = maxPixelSize;
	_original_width = _orig_image.Width();
	_original_height = _orig_image.Height();
	_has_full_resolution = (maxPixelSize == 0);  // 0 means full resolution

	_scaled_image.Resize();
	_scaled_image_scale = -1;
	_scale = -1;
	_rotate = _rotated = 0;
	_fine_rotate = 0;
	_mirror_h = _mirrored_h = _mirror_v = _mirrored_v = false;
	_base_dirty = true;
	_fine_dirty = false;

	if (use_orientation && orientation > 1 && orientation <= 8) {
		ApplyEXIFOrientation(orientation);
	}

	return true;
}

bool ImageView::EnsureFullResolution()
{
	// Already have full resolution?
	if (_has_full_resolution) {
		return true;
	}

	// Need to re-decode at full resolution (0 = no limit)
	DBG("Re-decoding at full resolution for zoom");
	if (!ReadImageInternal(0)) {
		return false;
	}

	_has_full_resolution = true;
	return true;
}

void ImageView::ApplyEXIFOrientation(int orientation)
{
	switch (orientation) {
		case 8: _rotate = -1; break;
		case 7: _mirror_h = true; _rotate = 1; break;
		case 6: _rotate = 1; break;
		case 5: _mirror_h = true; _rotate = -1; break;
		case 4: _mirror_v = true; break;
		case 3: _rotate = 2; break;
		case 2: _mirror_h = true; break;
		case 1: break;
		default:
			break;  // Unknown orientation, ignore
	}
}

bool ImageView::RefreshWGI()
{
	if (!WINPORT(GetConsoleImageCaps)(NULL, sizeof(_wgi), &_wgi)) {
		_err_str = "GetConsoleImageCaps failed";
		return false;
	}
	if ((_wgi.Caps & WP_IMGCAP_RGBA) == 0) {
		_err_str = "backend doesn't support graphics";
		return false;
	}
	if (_wgi.PixPerCell.X <= 0 || _wgi.PixPerCell.Y <= 0) {
		_err_str = StrPrintf("bad cell size ( %d x %d )", _wgi.PixPerCell.X, _wgi.PixPerCell.Y);
		return false;
	}
	return true;
}

void ImageView::SetupInitialScale(const int canvas_w, const int canvas_h)
{
	auto rotated_orig_w = _orig_image.Width();
	auto rotated_orig_h = _orig_image.Height();
	if ((_rotate % 2) != 0) {
		std::swap(rotated_orig_w, rotated_orig_h);
	}

	_scale_fit = std::min(double(canvas_w) / double(rotated_orig_w), double(canvas_h) / double(rotated_orig_h));
	_scale_max = std::max(_scale_fit * 4.0, 2.0);
	_scale_min = std::min(_scale_fit / 8.0, 0.5);

	const auto set_defscale = g_settings.GetDefaultScale();
	if (set_defscale == Settings::EQUAL_IMAGE) {
		_scale = 1.0;
	} else if (set_defscale == Settings::EQUAL_SCREEN || canvas_w < rotated_orig_w || canvas_h < rotated_orig_h) {
		_scale = _scale_fit;
	} else {
		_scale = 1.0;
	}
}

bool ImageView::EnsureReadyAndScaled()
{
	assert(_scale > 0);
	if (!_force_render && _scaled_image_scale > 0 && fabs(_scale -_scaled_image_scale) < 0.001) {
		return false;
	}
	_orig_image.Scale(_scaled_image, _scale);
	_scaled_image_scale = _scale;
	_rotated = 0;
	_mirrored_h = _mirrored_v = false;
	_force_render = false;
	_base_dirty = true;  // Need to rebuild base with new scale
	return true;
}

uint16_t ImageView::EnsureTransformed()
{
	static_assert(0 == WP_IMGTF_ROTATE0);
	uint16_t out = 0;

	// Step 1: Build _base_image from _scaled_image with mirrors and 90° rotations
	// This happens when scale changes or when _base_dirty is set
	if (_base_dirty) {
		_base_image = _scaled_image;  // Copy clean scaled image

		// Apply mirrors
		if (_mirror_h) {
			_base_image.MirrorH();
			_mirrored_h = true;
			out|= WP_IMGTF_MIRROR_H;
		}
		if (_mirror_v) {
			_base_image.MirrorV();
			_mirrored_v = true;
			out|= WP_IMGTF_MIRROR_V;
		}

		// Apply 90° rotations
		int rotate_steps = _rotate;
		while (rotate_steps < 0) rotate_steps += 4;
		rotate_steps %= 4;

		for (int i = 0; i < rotate_steps; ++i) {
			_base_image.Rotate(_tmp_image, true);
			_base_image.Swap(_tmp_image);
		}
		_rotated = _rotate;

		switch (rotate_steps) {
			case 1: out|= WP_IMGTF_ROTATE90; break;
			case 2: out|= WP_IMGTF_ROTATE180; break;
			case 3: out|= WP_IMGTF_ROTATE270; break;
		}

		_base_dirty = false;
	}

	// Step 2: Apply fine rotation from _base_image to _ready_image (single pass)
	// Use fast mode for fine rotation (angles are small, quality difference is minimal)
	if (_fine_rotate != 0) {
		_base_image.RotateArbitrary(_ready_image, _fine_rotate, false);
	} else {
		_ready_image = _base_image;
	}

	return out;
}

bool ImageView::SendWholeImage(const SMALL_RECT *area, const Image &img)
{
	static std::atomic<size_t> s_avg_speed{};
	size_t avg_speed = s_avg_speed;
	if (!avg_speed) {
		avg_speed = SETIMG_INITALLY_ASSUMED_SPEED;
	}
	auto msec = GetProcessUptimeMSec();
	auto chunk_h = img.Height();
	if ((_wgi.Caps & WP_IMGCAP_ATTACH) != 0 && avg_speed != 0) {
		auto estimated_time = img.Size() / avg_speed;
		if (estimated_time >= 2 * SETIMG_DELAY_BASELINE_MSEC) {
			chunk_h = std::max(32, int(img.Height() / (estimated_time / SETIMG_DELAY_BASELINE_MSEC)));
		}
	}

	for (int sent_h = 0; sent_h < img.Height(); ) {
		if ((_cancel && *_cancel) || (!_cancel && CheckForEscAndPurgeAccumulatedInputEvents())) {
			_err_str = "manually cancelled";
			return false;
		}
		auto set_h = img.Height() - sent_h;
		if (set_h > chunk_h + chunk_h / 4) {
			set_h = chunk_h;
		}
		if (!WINPORT(SetConsoleImage)(NULL, WINPORT_IMAGE_ID,
				WP_IMG_RGB | WP_IMG_PIXEL_OFFSET | (sent_h ? WP_IMG_ATTACH_BOTTOM : 0),
				area, img.Width(), set_h, img.Ptr(0, sent_h))) {
			_err_str = "failed to send image to terminal";
			return false;
		}
		sent_h+= set_h;
	}
	msec = GetProcessUptimeMSec() - msec;
	if (img.Size() >= SETIMG_ESTIMATION_SIZE_THRESHOLD && msec >= 1) {
		const size_t cur_speed = std::max(size_t(img.Size() / msec), size_t(1));
		size_t speed = s_avg_speed;
		if (speed < cur_speed || (speed - cur_speed > speed / 4) || speed == 0) {
			speed+= cur_speed;
			if (speed != cur_speed) {
				speed/= 2;
			}
			s_avg_speed = speed;
		}
	}
	return true;
}

bool ImageView::SendWholeViewport(const SMALL_RECT *area, int src_left, int src_top, int viewport_w, int viewport_h)
{
	if (src_left == 0 && src_top == 0 && viewport_w == _ready_image.Width() && viewport_h == _ready_image.Height()) {
		return SendWholeImage(area, _ready_image);
	}

	_tmp_image.Resize(viewport_w, viewport_h, _ready_image.BytesPerPixel());
	_ready_image.Blit(_tmp_image, 0, 0, viewport_w, viewport_h, src_left, src_top);
	return SendWholeImage(area, _tmp_image);
}

bool ImageView::SendScrollAttachH(const SMALL_RECT *area, int src_left, int src_top, int viewport_w, int viewport_h, int delta)
{
	_tmp_image.Resize(abs(delta), viewport_h, _ready_image.BytesPerPixel());

	DWORD64 flags = WP_IMG_RGB | WP_IMG_PIXEL_OFFSET | WP_IMG_SCROLL;
	if (delta > 0) {
		_ready_image.Blit(_tmp_image, 0, 0, delta, viewport_h, src_left, src_top);
		flags|= WP_IMG_ATTACH_LEFT;
	} else {
		_ready_image.Blit(_tmp_image, 0, 0, -delta, viewport_h, src_left + viewport_w + delta, src_top);
		flags|= WP_IMG_ATTACH_RIGHT;
	}

	return WINPORT(SetConsoleImage)(NULL, WINPORT_IMAGE_ID, flags, area,
		_tmp_image.Width(), _tmp_image.Height(), _tmp_image.Data()) != FALSE;
}

bool ImageView::SendScrollAttachV(const SMALL_RECT *area, int src_left, int src_top, int viewport_w, int viewport_h, int delta)
{
	_tmp_image.Resize(viewport_w, abs(delta), _ready_image.BytesPerPixel());

	DWORD64 flags = WP_IMG_RGB | WP_IMG_PIXEL_OFFSET | WP_IMG_SCROLL;
	if (delta > 0) {
		_ready_image.Blit(_tmp_image, 0, 0, viewport_w, delta, src_left, src_top);
		flags|= WP_IMG_ATTACH_TOP;
	} else {
		_ready_image.Blit(_tmp_image, 0, 0, viewport_w, -delta, src_left, src_top + viewport_h + delta);
		flags|= WP_IMG_ATTACH_BOTTOM;
	}

	return WINPORT(SetConsoleImage)(NULL, WINPORT_IMAGE_ID, flags, area,
		_tmp_image.Width(), _tmp_image.Height(), _tmp_image.Data()) != FALSE;
}

static int ShiftPercentsToPixels(int &percents, int width, int limit)
{
	int pixels = long(width) * percents / 100;
	if (abs(pixels) > limit) {
		pixels = (percents < 0) ? -limit : limit;
		percents = long(pixels) * 100 / width;
		percents+= (percents > 0) ? 1 : -1;
	}
	return pixels;
}

bool ImageView::RenderImage()
{
	if (_render_file.empty()) {
		_err_str = "bad file";
		return false;
	}

	if (_pos.X < 0 || _pos.Y < 0 || _size.X <= 0 || _size.Y <= 0) {
		_err_str = "bad grid";
		return false;
	}

	if (!RefreshWGI()) {
		return false;
	}

	int canvas_w = int(_size.X) * _wgi.PixPerCell.X;
	int canvas_h = int(_size.Y) * _wgi.PixPerCell.Y;

	if (_scale <= 0) {
		DenoteState("Rendering...");
		SetupInitialScale(canvas_w, canvas_h);
	}

	const auto scaled = EnsureReadyAndScaled();
	const auto tformed = EnsureTransformed();
	const bool fine_active = (_fine_rotate != 0);  // Fine rotation requires full re-render

	SMALL_RECT area = {_pos.X, _pos.Y, 0, 0};
	int viewport_w = canvas_w, viewport_h = canvas_h;
	int src_left = 0, src_top = 0;

	if (viewport_w > _ready_image.Width()) {
		auto margin = (viewport_w - _ready_image.Width()) / 2;
		area.Left+= margin / _wgi.PixPerCell.X;
		area.Right = margin % _wgi.PixPerCell.X;
		viewport_w = _ready_image.Width();
		_dx = 0;
	} else {
		src_left = (_ready_image.Width() - viewport_w) / 2;
		src_left+= ShiftPercentsToPixels(_dx, _ready_image.Width(), (_ready_image.Width() - viewport_w) / 2);
	}

	if (viewport_h > _ready_image.Height()) {
		auto margin = (viewport_h - _ready_image.Height()) / 2;
		area.Top+= margin / _wgi.PixPerCell.Y;
		area.Bottom = margin % _wgi.PixPerCell.Y;
		viewport_h = _ready_image.Height();
		_dy = 0;
	} else {
		src_top = (_ready_image.Height() - viewport_h) / 2;
		src_top+= ShiftPercentsToPixels(_dy, _ready_image.Height(), (_ready_image.Height() - viewport_h) / 2);
	}

	bool out = true;
	// Fine rotation always requires full re-render (can't use terminal-side transforms)
	if (_fine_dirty || fine_active) {
		out = SendWholeViewport(&area, src_left, src_top, viewport_w, viewport_h);
		_fine_dirty = false;
	} else if (!scaled && _prev_left == src_left && _prev_top == src_top && _dx == 0 && _dy == 0 && tformed != 0
			&& ((tformed & WP_IMGTF_MASK_ROTATE) == WP_IMGTF_ROTATE0 ||
				(_ready_image.Width() <= std::min(canvas_w, canvas_h) && _ready_image.Height() <= std::min(canvas_w, canvas_h)))
			&& (_wgi.Caps & WP_IMGCAP_ROTMIR) != 0) {
		out = WINPORT(TransformConsoleImage)(NULL, WINPORT_IMAGE_ID, &area, tformed) != FALSE;
	} else if (!scaled && tformed == 0 && abs(_prev_left - src_left) < viewport_w && abs(_prev_top - src_top) < viewport_h
			&& (_wgi.Caps & WP_IMGCAP_ATTACH) != 0 && (_wgi.Caps & WP_IMGCAP_SCROLL) != 0) {
		if (_prev_left != src_left) {
			out = SendScrollAttachH(&area, src_left, _prev_top, viewport_w, viewport_h, _prev_left - src_left);
		}
		if (_prev_top != src_top) {
			out = SendScrollAttachV(&area, src_left, src_top, viewport_w, viewport_h, _prev_top - src_top);
		}
	} else if (scaled || tformed != 0 || _prev_left != src_left || _prev_top != src_top) {
		out = SendWholeViewport(&area, src_left, src_top, viewport_w, viewport_h);
	}
	if (!out) {
		return false;
	}
	_prev_left = src_left;
	_prev_top = src_top;
	return true;
}

void ImageView::DenoteState(const char *stage)
{
	std::string info;
	if (stage) {
		info = stage;
	} else {
		if (_orig_image.Width() != 0 || _orig_image.Height() != 0) {
			info = std::to_string(_orig_image.Width()) + 'x' + std::to_string(_orig_image.Height());
		}
		if (!_file_size_str.empty()) {
			info+= (info.empty() ? "" : ", ") + _file_size_str;
		}
	}

	if (!info.empty()) {
		info.insert(0, 1, ' ');
		info+= ' ';
	}

	std::string pan;
	if (_mirror_h && !_mirror_v) {
		pan+= "🮛 ";
	} else if (_mirror_v && !_mirror_h) {
		pan+= "🮚 ";
	} else if (_mirror_v && _mirror_h) {
		pan+= "🮽 ";
	}
	// Show total rotation angle (90° steps + fine), normalized to 0..360
	int total_angle = _rotate * 90 + (int)round(_fine_rotate);
	while (total_angle < 0) total_angle += 360;
	while (total_angle >= 360) total_angle -= 360;
	if (total_angle != 0) {
		pan+= StrPrintf("%d° ", total_angle);
	}
	if (_scale > 0) {
		const char c1 = (_scale - _scale_fit > 0.01) ? '>' : ((_scale - _scale_fit < -0.01) ? '<' : '[');
		const char c2 = (_scale - _scale_fit > 0.01) ? '<' : ((_scale - _scale_fit < -0.01) ? '>' : ']');
		pan+= StrPrintf("%c%d%%%c ", c1, int(_scale * 100), c2);
	}
	if (_dx != 0 || _dy != 0) {
		pan+= StrPrintf("%s%d:%s%d ", (_dx > 0) ? "+" : "", _dx, (_dy > 0) ? "+" : "", _dy);
	}
	if (!pan.empty()) {
		pan.insert(0, 1, ' ');
	}

	DenoteInfoAndPan(info, pan);
}

void ImageView::DenoteInfoAndPan(const std::string &info, const std::string &pan)
{
	// Base implementation does nothing - override in subclass for UI updates
}

void ImageView::JustReset(bool keep_rotmir)
{
	_dx = _dy = 0;
	_scale = -1;
	_base_dirty = true;
	if (!keep_rotmir) {
		_rotate = 0;
		_fine_rotate = 0;
		_mirror_h = _mirror_v = false;
	}
}

ImageView::ImageView(size_t initial_file, const std::vector<std::pair<std::string, bool> > &all_files)
	: _all_files(all_files), _initial_file(initial_file), _cur_file(initial_file)
{
	assert(_initial_file < all_files.size());
}

ImageView::~ImageView()
{
	WINPORT(DeleteConsoleImage)(NULL, WINPORT_IMAGE_ID);
	if (!_tmp_file.empty()) {
		unlink(_tmp_file.c_str());
	}
}

std::unordered_set<std::string> ImageView::GetSelection() const
{
	std::unordered_set<std::string> out;
	for (const auto &it : _all_files) {
		if (it.second) {
			out.insert(it.first);
		}
	}
	return out;
}

SMALL_RECT ImageView::GetPreferredRect(const SMALL_RECT &screen_rect) const
{
	// Calculate preferred dialog size based on image size
	// Use _orig_image dimensions if _ready_image not yet scaled
	int img_w = _ready_image.Width() > 0 ? _ready_image.Width() : _orig_image.Width();
	int img_h = _ready_image.Height() > 0 ? _ready_image.Height() : _orig_image.Height();

	if (img_w == 0 || img_h == 0 || !_wgi.PixPerCell.X || !_wgi.PixPerCell.Y) {
		return screen_rect;  // Fallback to full screen
	}

	// Image size in cells (add 2 for frame borders)
	int img_cells_w = (img_w + _wgi.PixPerCell.X - 1) / _wgi.PixPerCell.X + 2;
	int img_cells_h = (img_h + _wgi.PixPerCell.Y - 1) / _wgi.PixPerCell.Y + 3;  // +3 for title and hint

	// Add extra for hint line at bottom
	int hint_height = 1;

	// Clamp to screen size
	int dlg_w = std::min(img_cells_w, (int)(screen_rect.Right - screen_rect.Left + 1));
	int dlg_h = std::min(img_cells_h + hint_height, (int)(screen_rect.Bottom - screen_rect.Top + 1));

	// Center on screen
	int dlg_x = screen_rect.Left + ((screen_rect.Right - screen_rect.Left + 1) - dlg_w) / 2;
	int dlg_y = screen_rect.Top + ((screen_rect.Bottom - screen_rect.Top + 1) - dlg_h) / 2;

	SMALL_RECT result;
	result.Left = dlg_x;
	result.Top = dlg_y;
	result.Right = dlg_x + dlg_w - 1;
	result.Bottom = dlg_y + dlg_h - 1;
	return result;
}

bool ImageView::Preload()
{
	// Just load the image without setting up for display
	if (!RefreshWGI()) {
		return false;
	}
	if (!ReadImage()) {
		return false;
	}
	return true;
}

bool ImageView::Setup(SMALL_RECT &rc, volatile bool *cancel)
{
	_cancel = cancel;
	_pos.X = rc.Left;
	_pos.Y = rc.Top;
	_size.X = rc.Right > rc.Left ? rc.Right - rc.Left + 1 : 1;
	_size.Y = rc.Bottom > rc.Top ? rc.Bottom - rc.Top + 1 : 1;

	_orig_image.Resize();
	_ready_image.Resize();
	_tmp_image.Resize();
	JustReset();

	// Get pixel info early for optimized decode+scale
	if (!RefreshWGI()) {
		return false;
	}

	_err_str.clear();
	if (!PrepareImage() || !RenderImage()) {
		return false;
	}
	DenoteState();
	return true;
}

void ImageView::Home()
{
	_cur_file = _initial_file;
	JustReset();
	if (PrepareImage() && RenderImage()) {
		DenoteState();
	}
}

void ImageView::Last()
{
	if (_all_files.empty()) return;
	_cur_file = _all_files.size() - 1;
	JustReset();
	if (PrepareImage() && RenderImage()) {
		DenoteState();
	}
}

bool ImageView::Iterate(bool forward)
{
	for (size_t i = 0;; ++i) {
		if (!IterateFile(forward) || i > _all_files.size()) {
			_cur_file = _initial_file;
			return false;
		}
		JustReset();
		if (PrepareImage() && RenderImage()) {
			DenoteState();
			return true;
		}
	}
}

void ImageView::Scale(int change)
{
	double ds = change * 4;
	if (fabs(_scale - _scale_fit) < 0.001) {
		if (change < 0) {
			ds*= (_scale_fit - _scale_min);
		} else {
			ds*= (_scale_max - _scale_fit);
		}
	} else if (_scale <= _scale_fit) {
		ds*= (_scale_fit - _scale_min);
	} else {
		ds*= (_scale_max - _scale_fit);
	}

	auto new_scale = _scale + ds / 100.0;
	if (new_scale < _scale_min) {
		new_scale = _scale_min;
	} else if (new_scale > _scale_max) {
		new_scale = _scale_max;
	}

	auto min_special = std::min(_scale_fit, 1.0);
	auto max_special = std::max(_scale_fit, 1.0);
	if (ds > 0) {
		if (_scale < min_special && new_scale > min_special) {
			new_scale = min_special;
		} else if (_scale < max_special && new_scale > max_special) {
			new_scale = max_special;
		}
	} else if (_scale > max_special && new_scale < max_special) {
		new_scale = max_special;
	} else if (_scale > min_special && new_scale < min_special) {
		new_scale = min_special;
	}

	if (_scale != new_scale) {
		// Check if we need full resolution for zooming in beyond 100%
		if (new_scale > 1.0 && !_has_full_resolution) {
			// Calculate required size at this zoom level
			int required_size = (int)(std::max(_original_width, _original_height) * new_scale);
			if (required_size > _decoded_max_size) {
				DBG("Zoom requires full resolution: scale=%.2f required=%d decoded=%d",
					new_scale, required_size, _decoded_max_size);
				if (!EnsureFullResolution()) {
					return;  // Failed to re-decode
				}
			}
		}
		_scale = new_scale;
		RenderImage();
		DenoteState();
	}
}

void ImageView::Rotate(int change)
{
	_rotate+= (change > 0) ? 1 : -1;
	_base_dirty = true;
	if (g_settings.AutoFitOnRotate()) {
		_scale = -1;  // Force recalculate fit
	}
	_force_render = true;  // Force recalculation of fit and full re-render
	RenderImage();
	DenoteState();
}

void ImageView::RotateFine(int degrees)
{
	_fine_rotate+= degrees;
	// Keep fine rotation in -45..45 range (use 90-degree steps for larger angles)
	bool changed_90 = false;
	while (_fine_rotate > 45) {
		_fine_rotate -= 90;
		_rotate++;
		changed_90 = true;
	}
	while (_fine_rotate < -45) {
		_fine_rotate += 90;
		_rotate--;
		changed_90 = true;
	}
	if (changed_90) {
		_base_dirty = true;
	}
	_fine_dirty = true;  // Force full re-render for fine rotation
	RenderImage();
	DenoteState();
}

void ImageView::Shift(int horizontal, int vertical)
{
	if (horizontal != 0) {
		_dx+= horizontal;
		if (_dx > 100) _dx = 100;
		if (_dx < -100) _dx = -100;
	}
	if (vertical != 0) {
		_dy+= vertical;
		if (_dy > 100) _dy = 100;
		if (_dy < -100) _dy = -100;
	}
	if (horizontal != 0 || vertical != 0) {
		RenderImage();
		DenoteState();
	}
}

COORD ImageView::ShiftByPixels(COORD delta)
{
	int saved_dx = _dx, saved_dy = _dy;
	Shift(int(delta.X) * 100 / _ready_image.Width(), int(delta.Y) * 100 / _ready_image.Height());
	return COORD {
		SHORT((_dx - saved_dx) * _ready_image.Width() / 100),
		SHORT((_dy - saved_dy) * _ready_image.Height() / 100)
	};
}

void ImageView::MirrorH()
{
	_mirror_h = !_mirror_h;
	_base_dirty = true;
	RenderImage();
	DenoteState();
}

void ImageView::MirrorV()
{
	_mirror_v = !_mirror_v;
	_base_dirty = true;
	RenderImage();
	DenoteState();
}

void ImageView::Reset(bool keep_rotmir)
{
	JustReset(keep_rotmir);
	RenderImage();
	DenoteState();
}

void ImageView::Select()
{
	if (!_all_files[_cur_file].second) {
		_all_files[_cur_file].second = true;
		DenoteState();
	}
}

void ImageView::Deselect()
{
	if (_all_files[_cur_file].second) {
		_all_files[_cur_file].second = false;
		DenoteState();
	}
}

void ImageView::ToggleSelection()
{
	_all_files[_cur_file].second = !_all_files[_cur_file].second;
	DenoteState();
}
