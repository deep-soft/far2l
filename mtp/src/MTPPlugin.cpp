#include "MTPPlugin.h"
#include "MTPLog.h"

#include <IntStrConv.h>
#include <WideMB.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>

PluginStartupInfo g_Info = {};
FarStandardFunctions g_FSF = {};

namespace {
std::string BackendTag(proto::BackendKind kind) {
    switch (kind) {
        case proto::BackendKind::MTP: return "MTP";
        case proto::BackendKind::PTP: return "PTP";
        default: return "UNKNOWN";
    }
}

std::string JoinPath(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/' || base.back() == '\\') {
        return base + name;
    }
    return base + "/" + name;
}

bool RemoveLocalPathRecursively(const std::string& path) {
    struct stat st = {};
    if (lstat(path.c_str(), &st) != 0) {
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            return false;
        }
        bool ok = true;
        while (true) {
            dirent* ent = readdir(dir);
            if (!ent) {
                break;
            }
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            if (!RemoveLocalPathRecursively(path + "/" + ent->d_name)) {
                ok = false;
                break;
            }
        }
        closedir(dir);
        if (!ok) {
            return false;
        }
        return rmdir(path.c_str()) == 0;
    }
    return unlink(path.c_str()) == 0;
}

FILETIME EpochToFileTime(uint64_t unix_epoch) {
    FILETIME ft{};
    if (unix_epoch == 0) {
        return ft;
    }
    constexpr uint64_t kEpochDiff = 11644473600ULL;
    uint64_t t = (unix_epoch + kEpochDiff) * 10000000ULL;
    ft.dwLowDateTime = static_cast<DWORD>(t & 0xffffffffULL);
    ft.dwHighDateTime = static_cast<DWORD>((t >> 32) & 0xffffffffULL);
    return ft;
}

const char* FormatCodeName(uint16_t format) {
    struct Entry {
        uint16_t code;
        const char* name;
    };
    static constexpr Entry kTable[] = {
        {0x3000, "Undefined Object"},
        {0x3001, "Association (Folder)"},
        {0x3002, "Script"},
        {0x3003, "Executable"},
        {0x3004, "Text"},
        {0x3005, "HTML"},
        {0x3006, "DPOF"},
        {0x3007, "AIFF"},
        {0x3008, "WAV"},
        {0x3009, "MP3"},
        {0x300A, "AVI"},
        {0x300B, "MPEG"},
        {0x300C, "ASF"},
        {0x300D, "Undefined Video"},
        {0x300E, "Undefined Audio"},
        {0x300F, "Undefined Collection"},
        {0x3800, "Undefined Image"},
        {0x3801, "EXIF/JPEG"},
        {0x3802, "TIFF/EP"},
        {0x3803, "FlashPix"},
        {0x3804, "BMP"},
        {0x3805, "CIFF"},
        {0x3807, "GIF"},
        {0x3808, "JFIF"},
        {0x3809, "PCD"},
        {0x380A, "PICT"},
        {0x380B, "PNG"},
        {0x380D, "TIFF"},
        {0x380E, "TIFF/IT"},
        {0x380F, "JP2"},
        {0x3810, "JPX"},
        {0xBA10, "Abstract Audio Album"},
        {0xBA11, "Abstract Audio Video Playlist"},
        {0xBA12, "Abstract Mediastream"},
        {0xBA13, "Abstract Audio Playlist"},
        {0xBA14, "Abstract Video Playlist"},
        {0xBA15, "Abstract Audio Album Playlist"},
        {0xBA16, "Abstract Image Album"},
        {0xBA17, "Abstract Image Album Playlist"},
        {0xBA18, "Abstract Video Album"},
        {0xBA19, "Abstract Video Album Playlist"},
        {0xBA1A, "Abstract Audio Video Album"},
        {0xBA1B, "Abstract Audio Video Album Playlist"},
        {0xBA1C, "Abstract Contact Group"},
        {0xBA1D, "Abstract Message Folder"},
        {0xBA1E, "Abstract Chaptered Production"},
        {0xBA1F, "Abstract Audio Podcast"},
        {0xBA20, "Abstract Video Podcast"},
        {0xBA21, "Abstract Audio Video Podcast"},
        {0xBA22, "Abstract Chaptered Production Playlist"},
        {0xBA81, "XML Document"},
        {0xBA82, "MS Word Document"},
        {0xBA83, "MHT Document"},
        {0xBA84, "MS Excel Spreadsheet"},
        {0xBA85, "MS PowerPoint Presentation"},
        {0xBB00, "vCard 2"},
        {0xBB01, "vCard 3"},
        {0xBB02, "vCalendar 1"},
        {0xBB03, "vCalendar 2"},
        {0xBB04, "vMessage"},
        {0xBB05, "Contact Database"},
        {0xBB06, "Message Database"},
        {0xB901, "WMA"},
        {0xB902, "OGG"},
        {0xB903, "AAC"},
        {0xB905, "FLAC"},
        {0xB906, "FLAC"},
        {0xB981, "WMV"},
        {0xB982, "MP4"},
        {0xB983, "3GP"},
        {0xB984, "3G2"},
    };
    for (const auto& e : kTable) {
        if (e.code == format) {
            return e.name;
        }
    }
    return nullptr;
}

bool NoControls(unsigned int control_state) {
    return (control_state & (PKF_CONTROL | PKF_ALT | PKF_SHIFT)) == 0;
}

uint32_t PackDeviceTriplet(uint8_t bus, uint8_t addr, uint8_t iface) {
    return (static_cast<uint32_t>(bus) << 16) |
           (static_cast<uint32_t>(addr) << 8) |
           static_cast<uint32_t>(iface);
}

bool UnpackDeviceTriplet(uint32_t packed, uint8_t& bus, uint8_t& addr, uint8_t& iface) {
    bus = static_cast<uint8_t>((packed >> 16) & 0xFFu);
    addr = static_cast<uint8_t>((packed >> 8) & 0xFFu);
    iface = static_cast<uint8_t>(packed & 0xFFu);
    return true;
}

uint32_t PackDeviceFromKey(const std::string& key) {
    int bus = -1;
    int addr = -1;
    int iface = -1;
    if (sscanf(key.c_str(), "%d:%d:%d", &bus, &addr, &iface) == 3 &&
        bus >= 0 && bus <= 255 &&
        addr >= 0 && addr <= 255 &&
        iface >= 0 && iface <= 255) {
        return PackDeviceTriplet(static_cast<uint8_t>(bus),
                                 static_cast<uint8_t>(addr),
                                 static_cast<uint8_t>(iface));
    }
    return 0;
}

std::string DeviceKeyFromPacked(uint32_t packed) {
    uint8_t bus = 0;
    uint8_t addr = 0;
    uint8_t iface = 0;
    if (!UnpackDeviceTriplet(packed, bus, addr, iface)) {
        return std::string();
    }
    return std::to_string(static_cast<int>(bus)) + ":" +
           std::to_string(static_cast<int>(addr)) + ":" +
           std::to_string(static_cast<int>(iface));
}
}

MTPPlugin::MTPPlugin(const wchar_t* path, bool path_is_standalone_config, int)
    : _transfer(new proto::TransferManager(nullptr)) {
    if (path && path_is_standalone_config) {
        _standalone_config = path;
    }
    _panel_title = L"MTP/PTP Devices";
    DBG("MTPPlugin constructed");
}

MTPPlugin::~MTPPlugin() {
    DBG("MTPPlugin destructed, current_device_key=%s", _current_device_key.c_str());
    if (_backend) {
        _backend->Disconnect();
        _router.Release(_current_device_key);
    }
}

int MTPPlugin::GetFindData(PluginPanelItem** panel_items, int* items_number, int) {
    DBG("GetFindData mode=%d", static_cast<int>(_view_mode));
    if (!panel_items || !items_number) {
        return FALSE;
    }

    switch (_view_mode) {
        case ViewMode::Devices:
            return ListDevices(panel_items, items_number);
        case ViewMode::Storages:
            return ListStorages(panel_items, items_number);
        case ViewMode::Objects:
            return ListObjects(panel_items, items_number);
    }
    return FALSE;
}

void MTPPlugin::FreeFindData(PluginPanelItem* panel_items, int items_number) {
    if (!panel_items || items_number <= 0) {
        return;
    }

    for (int i = 0; i < items_number; ++i) {
        free((void*)panel_items[i].FindData.lpwszFileName);
        free((void*)panel_items[i].Description);
    }
    free(panel_items);
}

void MTPPlugin::GetOpenPluginInfo(OpenPluginInfo* info) {
    if (!info) {
        return;
    }

    info->StructSize = sizeof(OpenPluginInfo);
    info->Flags = OPIF_SHOWPRESERVECASE | OPIF_USEHIGHLIGHTING | OPIF_ADDDOTS;
    info->HostFile = nullptr;
    info->CurDir = L"/";
    info->Format = L"libusb-mtp-ptp";
    info->PanelTitle = _panel_title.c_str();
    info->InfoLines = nullptr;
    info->DescrFiles = nullptr;
    info->PanelModesArray = nullptr;
    info->PanelModesNumber = 0;
    info->StartPanelMode = 0;
    info->StartSortMode = SM_NAME;
    info->StartSortOrder = 0;
    info->KeyBar = nullptr;
    info->ShortcutData = nullptr;
}

void MTPPlugin::UpdateObjectsPanelTitle() {
    std::string title = _current_storage_name.empty() ? "Objects" : _current_storage_name;
    for (const auto& part : _dir_stack) {
        if (part.empty()) {
            continue;
        }
        if (!title.empty()) {
            title += "/";
        }
        title += part;
    }
    if (title.empty()) {
        title = "Objects";
    }
    _panel_title = StrMB2Wide(title);
}

int MTPPlugin::SetDirectory(const wchar_t* dir, int) {
    DBG("SetDirectory dir=%s view_mode=%d cur_dev=%s cur_storage=%u cur_parent=%u",
        dir ? StrWide2MB(dir).c_str() : "(null)",
        static_cast<int>(_view_mode),
        _current_device_key.c_str(),
        _current_storage_id,
        _current_parent);
    if (!dir) {
        return FALSE;
    }

    std::string d = StrWide2MB(dir);
    if (d == "/") {
        _view_mode = ViewMode::Devices;
        _current_storage_id = 0;
        _current_parent = 0;
        _current_storage_name.clear();
        _current_device_name.clear();
        _dir_stack.clear();
        _name_token_index.clear();
        if (_backend) {
            _backend->Disconnect();
            _router.Release(_current_device_key);
            _backend.reset();
            _transfer->SetBackend(nullptr);
        }
        _current_device_key.clear();
        _panel_title = L"MTP/PTP Devices";
        return TRUE;
    }

    if (d == "..") {
        if (_view_mode == ViewMode::Objects && _current_parent != 0) {
            const uint32_t prev = _current_parent;
            auto st = _backend->Stat(_current_parent);
            if (st.ok) {
                _current_parent = st.value.parent;
            } else {
                _current_parent = 0;
            }
            if (!_dir_stack.empty()) {
                _dir_stack.pop_back();
            }
            UpdateObjectsPanelTitle();
            DBG("SetDirectory back object=%u new_parent=%u", prev, _current_parent);
            return TRUE;
        }
        if (_view_mode == ViewMode::Objects && _current_storage_id != 0) {
            _view_mode = ViewMode::Storages;
            _current_parent = 0;
            _current_storage_id = 0;
            _current_storage_name.clear();
            _dir_stack.clear();
            _panel_title = StrMB2Wide(_current_device_name.empty() ? "Storages" : _current_device_name);
            return TRUE;
        }
        if (_view_mode == ViewMode::Storages) {
            _view_mode = ViewMode::Devices;
            _current_storage_id = 0;
            _current_parent = 0;
            _name_token_index.clear();
            _current_storage_name.clear();
            _current_device_name.clear();
            _dir_stack.clear();
            if (_backend) {
                _backend->Disconnect();
                _router.Release(_current_device_key);
                _backend.reset();
                _transfer->SetBackend(nullptr);
            }
            _current_device_key.clear();
            _panel_title = L"MTP/PTP Devices";
            return TRUE;
        }
        return TRUE;
    }

    auto it = _name_token_index.find(d);
    if (it == _name_token_index.end() || it->second.empty()) {
        DBG("SetDirectory unresolved dir=%s", d.c_str());
        return FALSE;
    }
    DBG("SetDirectory dir=%s token=%s", d.c_str(), it->second.c_str());
    return EnterByToken(it->second) ? TRUE : FALSE;
}

int MTPPlugin::ProcessKey(int key, unsigned int control_state) {
    DBG("ProcessKey key=%d ctrl=0x%x mode=%d", key, control_state, static_cast<int>(_view_mode));
    if (key == VK_F3 && NoControls(control_state)) {
        return ExecuteSelected(OPM_VIEW) ? TRUE : FALSE;
    }
    if (key == VK_F4 && NoControls(control_state)) {
        return ExecuteSelected(OPM_EDIT) ? TRUE : FALSE;
    }
    if ((key == VK_F5 || key == VK_F6) && NoControls(control_state)) {
        if (CrossPanelCopyMoveSameDevice(key == VK_F6)) {
            return TRUE;
        }
    }
    if (key == VK_F6 && control_state == PKF_SHIFT) {
        return RenameSelectedItem() ? TRUE : FALSE;
    }
    return FALSE;
}

int MTPPlugin::ProcessEvent(int event, void* param) {
    (void)param;
    if (!_backend || !_backend->IsReady()) {
        return FALSE;
    }

    // Poll transport events on idle redraw cycles to invalidate stale cache on device-side changes.
    if (event == FE_IDLE) {
        _backend->PollEvents();
    }
    return FALSE;
}

int MTPPlugin::MakeDirectory(const wchar_t** name, int) {
    if (!_backend || !_backend->IsReady() || !name || !*name) {
        return FALSE;
    }

    std::string dir = StrWide2MB(*name);
    auto st = _backend->MakeDirectory(dir, _current_storage_id, _current_parent);
    if (!st.ok) {
        SetErrorFromStatus(st);
        return FALSE;
    }
    return TRUE;
}

int MTPPlugin::DeleteFiles(PluginPanelItem* panel_item, int items_number, int) {
    if (!_backend || !_backend->IsReady() || !panel_item || items_number <= 0) {
        return FALSE;
    }

    int okCount = 0;
    int lastErr = 0;
    for (int i = 0; i < items_number; ++i) {
        std::string token;
        if (!ResolvePanelToken(panel_item[i], token)) {
            continue;
        }
        uint32_t handle = 0;
        if (!ParseObjectToken(token, handle)) {
            continue;
        }

        auto st = _backend->Delete(handle, true);
        if (st.ok) {
            ++okCount;
        } else {
            lastErr = MapErrorToErrno(st);
        }
    }

    if (okCount == 0 && lastErr != 0) {
        WINPORT(SetLastError)(lastErr);
    }
    return okCount > 0 ? TRUE : FALSE;
}

int MTPPlugin::GetFiles(PluginPanelItem* panel_item, int items_number, int, const wchar_t** dest_path, int) {
    if (!_backend || !_backend->IsReady() || !_transfer || !panel_item || items_number <= 0 || !dest_path || !dest_path[0]) {
        return FALSE;
    }

    const std::string baseDest = StrWide2MB(dest_path[0]);
    proto::CancellationSource cancelSrc;

    int okCount = 0;
    int lastErr = 0;
    for (int i = 0; i < items_number; ++i) {
        std::string token;
        if (!ResolvePanelToken(panel_item[i], token)) {
            continue;
        }

        uint32_t handle = 0;
        if (!ParseObjectToken(token, handle)) {
            continue;
        }

        auto st = _backend->Stat(handle);
        if (!st.ok) {
            lastErr = MapErrorToErrno(st.code);
            continue;
        }

        const std::string localPath = JoinPath(baseDest, st.value.name);
        proto::Status op;
        if (st.value.is_dir) {
            uint64_t done = 0;
            uint64_t total = 0;
            op = DownloadRecursive(st.value.storage_id, st.value.handle, localPath, cancelSrc.Token(), done, total);
        } else {
            op = _transfer->Download(st.value.handle,
                                     localPath,
                                     [](uint64_t, uint64_t) {},
                                     cancelSrc.Token());
        }

        if (op.ok) {
            ++okCount;
        } else {
            lastErr = MapErrorToErrno(op);
        }
    }

    if (okCount == 0 && lastErr != 0) {
        WINPORT(SetLastError)(lastErr);
    }

    return okCount > 0 ? TRUE : FALSE;
}

int MTPPlugin::PutFiles(PluginPanelItem* panel_item, int items_number, int move, const wchar_t* src_path, int) {
    if (!_backend || !_backend->IsReady() || !panel_item || items_number <= 0 || !src_path) {
        return FALSE;
    }
    if (_view_mode != ViewMode::Objects || _current_storage_id == 0) {
        WINPORT(SetLastError)(EINVAL);
        return FALSE;
    }

    std::string baseSrc = StrWide2MB(src_path);
    if (baseSrc.empty()) {
        WINPORT(SetLastError)(EINVAL);
        return FALSE;
    }

    proto::CancellationSource cancelSrc;
    int successCount = 0;
    int lastErr = 0;

    for (int i = 0; i < items_number; ++i) {
        if (!panel_item[i].FindData.lpwszFileName) {
            continue;
        }
        std::string fileName = StrWide2MB(panel_item[i].FindData.lpwszFileName);
        if (fileName.empty() || fileName == "." || fileName == "..") {
            continue;
        }

        std::string localPath = JoinPath(baseSrc, fileName);
        auto st = _backend->Upload(localPath,
                                   fileName,
                                   _current_storage_id,
                                   _current_parent,
                                   [](uint64_t, uint64_t) {},
                                   cancelSrc.Token());
        if (st.ok) {
            ++successCount;
            if (move) {
                (void)RemoveLocalPathRecursively(localPath);
            }
        } else {
            lastErr = MapErrorToErrno(st);
        }
    }

    if (successCount == 0 && lastErr != 0) {
        WINPORT(SetLastError)(lastErr);
    }
    return successCount > 0 ? TRUE : FALSE;
}

int MTPPlugin::Execute(PluginPanelItem* panel_item, int items_number, int op_mode) {
    if (!_backend || !_backend->IsReady() || !panel_item || items_number <= 0) {
        return FALSE;
    }
    const bool isView = ((op_mode & OPM_VIEW) != 0) || ((op_mode & OPM_QUICKVIEW) != 0);
    const bool isEdit = ((op_mode & OPM_EDIT) != 0);
    if (!isView && !isEdit) {
        return FALSE;
    }

    std::string token;
    if (!ResolvePanelToken(panel_item[0], token)) {
        return FALSE;
    }
    uint32_t handle = 0;
    if (!ParseObjectToken(token, handle)) {
        return FALSE;
    }

    auto st = _backend->Stat(handle);
    if (!st.ok || st.value.is_dir) {
        return FALSE;
    }

    std::string localPath = "/tmp/mtp_view_" + std::to_string(st.value.handle) + "_" + st.value.name;
    proto::CancellationSource cancelSrc;
    auto dl = _transfer->Download(st.value.handle, localPath, [](uint64_t, uint64_t) {}, cancelSrc.Token());
    if (!dl.ok) {
        SetErrorFromStatus(dl);
        return FALSE;
    }

    std::wstring wpath = StrMB2Wide(localPath);
    std::wstring wtitle = StrMB2Wide(st.value.name);
    if (isEdit) {
        g_Info.Editor(wpath.c_str(),
                      wtitle.empty() ? nullptr : wtitle.c_str(),
                      -1, -1, -1, -1,
                      EF_NONMODAL,
                      -1,
                      -1,
                      CP_UTF8);
    } else {
        g_Info.Viewer(wpath.c_str(),
                      wtitle.empty() ? nullptr : wtitle.c_str(),
                      -1, -1, -1, -1,
                      VF_NONMODAL | VF_DELETEONCLOSE,
                      CP_UTF8);
    }
    return TRUE;
}

PluginStartupInfo* MTPPlugin::GetInfo() {
    return &g_Info;
}

PluginPanelItem MTPPlugin::MakePanelItem(const std::string& name,
                                         bool is_dir,
                                         uint64_t size,
                                         uint64_t mtime_epoch,
                                         uint32_t object_id,
                                         uint32_t storage_id,
                                         uint32_t packed_device_id,
                                         const std::string& description,
                                         uint64_t ctime_epoch,
                                         uint32_t file_attributes,
                                         uint32_t unix_mode) const {
    PluginPanelItem item = {{{0}}};

    std::wstring wname = StrMB2Wide(name);
    item.FindData.lpwszFileName = static_cast<wchar_t*>(calloc(wname.size() + 1, sizeof(wchar_t)));
    if (item.FindData.lpwszFileName) {
        wcscpy(const_cast<wchar_t*>(item.FindData.lpwszFileName), wname.c_str());
    }

    if (file_attributes == 0) {
        file_attributes = is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    if (unix_mode == 0) {
        unix_mode = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    }
    item.FindData.dwFileAttributes = file_attributes;
    item.FindData.dwUnixMode = unix_mode;
    item.FindData.nFileSize = size;
    item.FindData.nPhysicalSize = size;
    if (ctime_epoch == 0) {
        ctime_epoch = mtime_epoch;
    }
    item.FindData.ftCreationTime = EpochToFileTime(ctime_epoch);
    item.FindData.ftLastAccessTime = item.FindData.ftCreationTime;
    item.FindData.ftLastWriteTime = item.FindData.ftCreationTime;
    if (mtime_epoch != 0) {
        item.FindData.ftLastWriteTime = EpochToFileTime(mtime_epoch);
        item.FindData.ftLastAccessTime = item.FindData.ftLastWriteTime;
    }

    item.UserData = static_cast<DWORD_PTR>(object_id);

    if (!description.empty()) {
        std::wstring wd = StrMB2Wide(description);
        item.Description = static_cast<wchar_t*>(calloc(wd.size() + 1, sizeof(wchar_t)));
        if (item.Description) {
            wcscpy(const_cast<wchar_t*>(item.Description), wd.c_str());
        }
    }
    item.Reserved[0] = static_cast<DWORD_PTR>(storage_id);
    item.Reserved[1] = static_cast<DWORD_PTR>(packed_device_id);

    return item;
}

std::string MTPPlugin::BuildObjectDescription(const proto::ObjectEntry& entry) {
    if (entry.format != 0) {
        const char* name = FormatCodeName(entry.format);
        if (name) {
            return std::string(name);
        }
        char fmt[32] = {};
        snprintf(fmt, sizeof(fmt), "Unknown (0x%04X)", entry.format);
        return std::string(fmt);
    }
    return std::string();
}

PluginPanelItem MTPPlugin::MakeObjectPanelItem(const proto::ObjectEntry& entry,
                                               uint32_t packed_device_id) const {
    uint32_t attrs = entry.is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (entry.is_hidden || (!entry.name.empty() && entry.name[0] == '.')) {
        attrs |= FILE_ATTRIBUTE_HIDDEN;
    }
    if (entry.is_readonly) {
        attrs |= FILE_ATTRIBUTE_READONLY;
    }

    uint32_t mode = entry.is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    if (entry.is_readonly) {
        mode = entry.is_dir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    }

    return MakePanelItem(entry.name,
                         entry.is_dir,
                         entry.size,
                         entry.mtime_epoch,
                         entry.handle,
                         entry.storage_id,
                         packed_device_id,
                         BuildObjectDescription(entry),
                         entry.ctime_epoch,
                         attrs,
                         mode);
}

bool MTPPlugin::GetSelectedPanelUserData(std::string& out) const {
    out.clear();
    intptr_t size = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, 0);
    DBG("GetSelectedPanelUserData size=%ld", static_cast<long>(size));
    if (size < static_cast<intptr_t>(sizeof(PluginPanelItem))) {
        return false;
    }

    PluginPanelItem* item = static_cast<PluginPanelItem*>(malloc(size + 0x100));
    if (!item) {
        return false;
    }

    memset(item, 0, size + 0x100);
    intptr_t ok = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, reinterpret_cast<LONG_PTR>(item));
    DBG("GetSelectedPanelUserData fetch_ok=%ld user_data=%llu storage=%llu dev=%llu name=%s",
        static_cast<long>(ok),
        static_cast<unsigned long long>(item->UserData),
        static_cast<unsigned long long>(item->Reserved[0]),
        static_cast<unsigned long long>(item->Reserved[1]),
        item->FindData.lpwszFileName ? StrWide2MB(item->FindData.lpwszFileName).c_str() : "(null)");
    if (ok) {
        (void)ResolvePanelToken(*item, out);
    }
    DBG("GetSelectedPanelUserData token=%s", out.c_str());
    free(item);
    return !out.empty();
}

bool MTPPlugin::GetSelectedPanelFileName(std::string& out) const {
    out.clear();
    intptr_t size = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, 0);
    if (size < static_cast<intptr_t>(sizeof(PluginPanelItem))) {
        return false;
    }

    PluginPanelItem* item = static_cast<PluginPanelItem*>(malloc(size + 0x100));
    if (!item) {
        return false;
    }
    memset(item, 0, size + 0x100);

    intptr_t ok = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, reinterpret_cast<LONG_PTR>(item));
    if (!ok) {
        DBG("GetSelectedPanelFileName fetch failed");
        free(item);
        return false;
    }

    if (item->FindData.lpwszFileName) {
        out = StrWide2MB(item->FindData.lpwszFileName);
    }
    DBG("GetSelectedPanelFileName ok name=%s", out.c_str());
    free(item);
    return !out.empty();
}

bool MTPPlugin::EnsureConnected(const std::string& device_key) {
    DBG("EnsureConnected device_key=%s", device_key.c_str());
    auto it = _device_binds.find(device_key);
    if (it == _device_binds.end()) {
        DBG("EnsureConnected missing device bind key=%s", device_key.c_str());
        return false;
    }

    if (_backend && _current_device_key == device_key && _backend->IsReady()) {
        return true;
    }

    if (_backend) {
        DBG("EnsureConnected disconnect previous key=%s", _current_device_key.c_str());
        _backend->Disconnect();
        _router.Release(_current_device_key);
    }

    DBG("EnsureConnected acquire backend kind=%d vid=%04x pid=%04x if=%u ep_in=0x%02x ep_out=0x%02x ep_int=0x%02x",
        static_cast<int>(it->second.kind),
        it->second.candidate.vendor_id,
        it->second.candidate.product_id,
        it->second.candidate.interface_number,
        it->second.candidate.endpoint_bulk_in,
        it->second.candidate.endpoint_bulk_out,
        it->second.candidate.endpoint_interrupt_in);
    _backend = _router.Acquire(it->second.candidate, it->second.kind);
    if (!_backend) {
        DBG("EnsureConnected acquire returned null");
        return false;
    }

    auto st = _backend->Connect();
    if (!st.ok) {
        DBG("Connect failed code=%d msg=%s", static_cast<int>(st.code), st.message.c_str());
        SetErrorFromStatus(st);
        _backend.reset();
        return false;
    }

    DBG("Connect success device_key=%s", device_key.c_str());
    _current_device_key = device_key;
    _transfer->SetBackend(_backend);
    return true;
}

int MTPPlugin::ListDevices(PluginPanelItem** panel_items, int* items_number) {
    auto devices = _router.EnumerateAndClassify();
    if (!devices.ok) {
        DBG("EnumerateAndClassify failed code=%d msg=%s", static_cast<int>(devices.code), devices.message.c_str());
        WINPORT(SetLastError)(MapErrorToErrno(devices.code));
        return FALSE;
    }
    DBG("EnumerateAndClassify found=%zu", devices.value.size());

    _device_binds.clear();

    std::vector<PluginPanelItem> out;
    out.reserve(devices.value.size());
    _name_token_index.clear();
    for (const auto& p : devices.value) {
        DeviceBind bind;
        bind.candidate = p.candidate;
        bind.kind = p.device.backend;
        _device_binds[p.device.key] = bind;

        std::string name = p.device.product.empty() ? "USB Device" : p.device.product;
        if (!p.device.serial.empty()) {
            name += " [" + p.device.serial + "]";
        }
        const std::string desc = BackendTag(p.device.backend) + " " +
                                 (p.device.manufacturer.empty() ? "" : p.device.manufacturer);

        std::string token = "DEV|" + p.device.key;
        const uint32_t packedDev = PackDeviceTriplet(static_cast<uint8_t>(p.candidate.id.bus),
                                                     static_cast<uint8_t>(p.candidate.id.address),
                                                     static_cast<uint8_t>(p.candidate.id.interface_number));
        DBG("ListDevices item name=%s token=%s backend=%d vid=%04x pid=%04x serial=%s",
            name.c_str(),
            token.c_str(),
            static_cast<int>(p.device.backend),
            p.device.vendor_id,
            p.device.product_id,
            p.device.serial.c_str());
        out.push_back(MakePanelItem(name, true, 0, 0, 0, 0, packedDev, desc));
        auto em = _name_token_index.emplace(name, token);
        if (!em.second) {
            em.first->second.clear();
        }
    }

    if (out.empty()) {
        out.push_back(MakePanelItem("No MTP/PTP USB devices found", false, 0, 0, 0, 0, 0));
    }

    *items_number = static_cast<int>(out.size());
    *panel_items = static_cast<PluginPanelItem*>(calloc(out.size(), sizeof(PluginPanelItem)));
    if (!*panel_items) {
        return FALSE;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        (*panel_items)[i] = out[i];
    }
    return TRUE;
}

int MTPPlugin::ListStorages(PluginPanelItem** panel_items, int* items_number) {
    if (!_backend || !_backend->IsReady()) {
        DBG("ListStorages backend not ready backend=%p", _backend.get());
        return FALSE;
    }

    auto storages = _backend->ListStorages();
    if (!storages.ok) {
        DBG("ListStorages failed code=%d msg=%s", static_cast<int>(storages.code), storages.message.c_str());
        WINPORT(SetLastError)(MapErrorToErrno(storages.code));
        return FALSE;
    }
    DBG("ListStorages count=%zu", storages.value.size());

    std::vector<PluginPanelItem> out;
    out.reserve(storages.value.size());
    const uint32_t packedDev = PackDeviceFromKey(_current_device_key);
    _name_token_index.clear();
    for (const auto& s : storages.value) {
        std::string name = s.description.empty() ? ("Storage " + std::to_string(s.id)) : s.description;
        std::string token = "STO|" + std::to_string(s.id);
        DBG("ListStorages item id=%u name=%s volume=%s free=%llu cap=%llu token=%s",
            s.id,
            name.c_str(),
            s.volume.c_str(),
            static_cast<unsigned long long>(s.free_bytes),
            static_cast<unsigned long long>(s.max_capacity),
            token.c_str());
        out.push_back(MakePanelItem(name, true, s.max_capacity, 0, 0, s.id, packedDev, s.volume));
        auto em = _name_token_index.emplace(name, token);
        if (!em.second) {
            em.first->second.clear();
        }
    }

    *items_number = static_cast<int>(out.size());
    *panel_items = static_cast<PluginPanelItem*>(calloc(out.size(), sizeof(PluginPanelItem)));
    if (!*panel_items) {
        return FALSE;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        (*panel_items)[i] = out[i];
    }
    return TRUE;
}

int MTPPlugin::ListObjects(PluginPanelItem** panel_items, int* items_number) {
    if (!_backend || !_backend->IsReady() || _current_storage_id == 0) {
        DBG("ListObjects precondition failed backend=%p ready=%d storage=%u parent=%u",
            _backend.get(),
            (_backend && _backend->IsReady()) ? 1 : 0,
            _current_storage_id,
            _current_parent);
        return FALSE;
    }

    auto children = _backend->ListChildren(_current_storage_id, _current_parent);
    if (!children.ok) {
        DBG("ListChildren failed storage=%u parent=%u code=%d msg=%s",
            _current_storage_id, _current_parent, static_cast<int>(children.code), children.message.c_str());
        WINPORT(SetLastError)(MapErrorToErrno(children.code));
        return FALSE;
    }
    DBG("ListChildren storage=%u parent=%u count=%zu", _current_storage_id, _current_parent, children.value.size());

    std::vector<PluginPanelItem> out;
    out.reserve(children.value.size());
    const uint32_t packedDev = PackDeviceFromKey(_current_device_key);
    _name_token_index.clear();
    for (const auto& e : children.value) {
        std::string token = "OBJ|" + std::to_string(e.handle);
        DBG("ListObjects item handle=%u parent=%u storage=%u dir=%d size=%llu name=%s token=%s",
            e.handle,
            e.parent,
            e.storage_id,
            e.is_dir ? 1 : 0,
            static_cast<unsigned long long>(e.size),
            e.name.c_str(),
            token.c_str());
        out.push_back(MakeObjectPanelItem(e, packedDev));
        auto em = _name_token_index.emplace(e.name, token);
        if (!em.second) {
            em.first->second.clear();
        }
    }

    *items_number = static_cast<int>(out.size());
    *panel_items = static_cast<PluginPanelItem*>(calloc(out.size(), sizeof(PluginPanelItem)));
    if (!*panel_items) {
        return FALSE;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        (*panel_items)[i] = out[i];
    }
    return TRUE;
}

bool MTPPlugin::ParseStorageToken(const std::string& token, uint32_t& storage_id) const {
    if (token.rfind("STO|", 0) != 0) {
        return false;
    }
    try {
        storage_id = static_cast<uint32_t>(std::stoul(token.substr(4)));
        return true;
    } catch (...) {
        return false;
    }
}

bool MTPPlugin::ParseObjectToken(const std::string& token, uint32_t& handle) const {
    if (token.rfind("OBJ|", 0) != 0) {
        return false;
    }
    try {
        handle = static_cast<uint32_t>(std::stoul(token.substr(4)));
        return true;
    } catch (...) {
        return false;
    }
}

bool MTPPlugin::ResolvePanelToken(const PluginPanelItem& item, std::string& token) const {
    token.clear();
    const uint32_t object_id = static_cast<uint32_t>(item.UserData);
    const uint32_t storage_id = static_cast<uint32_t>(item.Reserved[0]);
    const uint32_t packed_dev = static_cast<uint32_t>(item.Reserved[1]);

    if (_view_mode == ViewMode::Objects) {
        if (object_id != 0) {
            token = "OBJ|" + std::to_string(object_id);
            return true;
        }
    } else if (_view_mode == ViewMode::Storages) {
        if (storage_id != 0) {
            token = "STO|" + std::to_string(storage_id);
            return true;
        }
    } else if (_view_mode == ViewMode::Devices) {
        if (packed_dev != 0) {
            const std::string key = DeviceKeyFromPacked(packed_dev);
            if (!key.empty()) {
                token = "DEV|" + key;
                return true;
            }
        }
    }

    if (object_id != 0) {
        token = "OBJ|" + std::to_string(object_id);
        return true;
    }
    if (storage_id != 0) {
        token = "STO|" + std::to_string(storage_id);
        return true;
    }
    if (packed_dev != 0) {
        const std::string key = DeviceKeyFromPacked(packed_dev);
        if (!key.empty()) {
            token = "DEV|" + key;
            return true;
        }
    }

    if (item.FindData.lpwszFileName) {
        std::string name = StrWide2MB(item.FindData.lpwszFileName);
        auto it = _name_token_index.find(name);
        if (it != _name_token_index.end() && !it->second.empty()) {
            token = it->second;
            return true;
        }
    }
    return false;
}

int MTPPlugin::MapErrorToErrno(const proto::Status& st) const {
    return MapErrorToErrno(st.code);
}

int MTPPlugin::MapErrorToErrno(proto::ErrorCode code) const {
    switch (code) {
        case proto::ErrorCode::NotFound: return ENOENT;
        case proto::ErrorCode::AccessDenied: return EACCES;
        case proto::ErrorCode::Busy: return EBUSY;
        case proto::ErrorCode::Timeout: return ETIMEDOUT;
        case proto::ErrorCode::InvalidArgument: return EINVAL;
        case proto::ErrorCode::Unsupported: return ENOTSUP;
        case proto::ErrorCode::Cancelled: return ECANCELED;
        case proto::ErrorCode::Disconnected: return ENODEV;
        default: return EIO;
    }
}

void MTPPlugin::SetErrorFromStatus(const proto::Status& st) const {
    WINPORT(SetLastError)(MapErrorToErrno(st));
}

bool MTPPlugin::PromptInput(const wchar_t* title,
                            const wchar_t* prompt,
                            const wchar_t* history_name,
                            const std::string& initial_value,
                            std::string& out) const {
    out.clear();

    wchar_t input_buffer[1024] = {0};
    if (!initial_value.empty()) {
        std::wstring w = StrMB2Wide(initial_value);
        wcsncpy(input_buffer, w.c_str(), (sizeof(input_buffer) / sizeof(input_buffer[0])) - 1);
    }

    const bool ok = g_Info.InputBox(title,
                                    prompt,
                                    history_name,
                                    nullptr,
                                    input_buffer,
                                    (sizeof(input_buffer) / sizeof(input_buffer[0])) - 1,
                                    nullptr,
                                    FIB_BUTTONS | FIB_NOUSELASTHISTORY);
    if (!ok) {
        return false;
    }
    out = StrWide2MB(input_buffer);
    return !out.empty();
}

void MTPPlugin::RefreshPanel() const {
    g_Info.Control(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, 0);
    g_Info.Control(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, 0);
}

bool MTPPlugin::RenameSelectedItem() {
    if (_view_mode != ViewMode::Objects || !_backend || !_backend->IsReady()) {
        return false;
    }

    std::string token;
    if (!GetSelectedPanelUserData(token)) {
        return false;
    }

    uint32_t handle = 0;
    if (!ParseObjectToken(token, handle)) {
        return false;
    }

    std::string old_name;
    if (!GetSelectedPanelFileName(old_name)) {
        old_name = "";
    }
    if (old_name == "." || old_name == "..") {
        return false;
    }

    std::string new_name;
    if (!PromptInput(L"Rename",
                     L"Enter new name:",
                     L"MTP_Rename",
                     old_name,
                     new_name)) {
        return false;
    }
    if (new_name == old_name) {
        return true;
    }

    auto st = _backend->Rename(handle, new_name);
    if (!st.ok) {
        SetErrorFromStatus(st);
        return false;
    }

    RefreshPanel();
    return true;
}

bool MTPPlugin::ExecuteSelected(int op_mode) {
    intptr_t size = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, 0);
    if (size < static_cast<intptr_t>(sizeof(PluginPanelItem))) {
        size = g_Info.Control(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, 0);
        if (size < static_cast<intptr_t>(sizeof(PluginPanelItem))) {
            return false;
        }
        PluginPanelItem* item = static_cast<PluginPanelItem*>(malloc(size + 0x100));
        if (!item) {
            return false;
        }
        memset(item, 0, size + 0x100);
        intptr_t ok = g_Info.Control(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, reinterpret_cast<LONG_PTR>(item));
        if (!ok) {
            free(item);
            return false;
        }
        const int rc = Execute(item, 1, op_mode);
        free(item);
        return rc != FALSE;
    }

    PluginPanelItem* item = static_cast<PluginPanelItem*>(malloc(size + 0x100));
    if (!item) {
        return false;
    }
    memset(item, 0, size + 0x100);
    intptr_t ok = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, reinterpret_cast<LONG_PTR>(item));
    if (!ok) {
        free(item);
        return false;
    }
    const int rc = Execute(item, 1, op_mode);
    free(item);
    return rc != FALSE;
}

bool MTPPlugin::CrossPanelCopyMoveSameDevice(bool move) {
    HANDLE active = INVALID_HANDLE_VALUE;
    g_Info.Control(PANEL_ACTIVE, FCTL_GETPANELPLUGINHANDLE, 0, reinterpret_cast<LONG_PTR>(&active));
    if (active != reinterpret_cast<HANDLE>(this)) {
        return false;
    }

    HANDLE passive = INVALID_HANDLE_VALUE;
    g_Info.Control(PANEL_PASSIVE, FCTL_GETPANELPLUGINHANDLE, 0, reinterpret_cast<LONG_PTR>(&passive));
    if (passive == INVALID_HANDLE_VALUE || passive == nullptr) {
        return false;
    }
    auto* dst = reinterpret_cast<MTPPlugin*>(passive);
    if (!dst || dst == this) {
        return false;
    }

    if (!_backend || !_backend->IsReady() || !dst->_backend || !dst->_backend->IsReady()) {
        return false;
    }
    if (_view_mode != ViewMode::Objects || dst->_view_mode != ViewMode::Objects) {
        return false;
    }
    if (_current_device_key.empty() || _current_device_key != dst->_current_device_key) {
        DBG("CrossPanelCopyMoveSameDevice skip: different devices src=%s dst=%s",
            _current_device_key.c_str(), dst->_current_device_key.c_str());
        return false;
    }
    if (_current_storage_id == 0 || dst->_current_storage_id == 0) {
        return false;
    }

    auto getItemByCmd = [&](HANDLE panel, int cmd, int idx, PluginPanelItem& out, std::vector<uint8_t>& buf) -> bool {
        intptr_t sz = g_Info.Control(panel, cmd, idx, 0);
        if (sz < static_cast<intptr_t>(sizeof(PluginPanelItem))) {
            return false;
        }
        buf.assign(static_cast<size_t>(sz + 0x100), 0);
        auto* item = reinterpret_cast<PluginPanelItem*>(buf.data());
        intptr_t ok = g_Info.Control(panel, cmd, idx, reinterpret_cast<LONG_PTR>(item));
        if (!ok) {
            return false;
        }
        out = *item;
        return true;
    };

    PanelInfo pi = {};
    g_Info.Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, reinterpret_cast<LONG_PTR>(&pi));

    std::vector<uint32_t> handles;
    std::string firstName;
    if (pi.SelectedItemsNumber > 0) {
        for (int i = 0; i < pi.SelectedItemsNumber; ++i) {
            PluginPanelItem item = {};
            std::vector<uint8_t> buf;
            if (!getItemByCmd(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, i, item, buf)) {
                continue;
            }
            std::string token;
            if (!ResolvePanelToken(item, token)) {
                continue;
            }
            uint32_t h = 0;
            if (!ParseObjectToken(token, h)) {
                continue;
            }
            auto st = _backend->Stat(h);
            if (!st.ok || st.value.name == "." || st.value.name == "..") {
                continue;
            }
            if (firstName.empty()) {
                firstName = st.value.name;
            }
            handles.push_back(h);
        }
    } else {
        PluginPanelItem item = {};
        std::vector<uint8_t> buf;
        if (getItemByCmd(PANEL_ACTIVE, FCTL_GETCURRENTPANELITEM, 0, item, buf)) {
            std::string token;
            if (ResolvePanelToken(item, token)) {
                uint32_t h = 0;
                if (ParseObjectToken(token, h)) {
                    auto st = _backend->Stat(h);
                    if (st.ok && st.value.name != "." && st.value.name != "..") {
                        firstName = st.value.name;
                        handles.push_back(h);
                    }
                }
            }
        }
    }

    if (handles.empty()) {
        DBG("CrossPanelCopyMoveSameDevice skip: no selected object handles");
        return false;
    }
    DBG("CrossPanelCopyMoveSameDevice op=%s count=%zu dst_storage=%u dst_parent=%u",
        move ? "move" : "copy", handles.size(), dst->_current_storage_id, dst->_current_parent);

    for (uint32_t h : handles) {
        proto::Status st;
        if (move) {
            st = _backend->MoveObject(h, dst->_current_storage_id, dst->_current_parent);
        } else {
            auto cp = _backend->CopyObject(h, dst->_current_storage_id, dst->_current_parent);
            if (!cp.ok) {
                st = proto::Status::Failure(cp.code, cp.message, cp.retryable);
            } else {
                st = proto::OkStatus();
            }
        }
        if (!st.ok) {
            DBG("CrossPanelCopyMoveSameDevice failed handle=%u code=%d msg=%s",
                h, static_cast<int>(st.code), st.message.c_str());
            SetErrorFromStatus(st);
            if (st.code == proto::ErrorCode::Unsupported) {
                const wchar_t* msg[] = {
                    move ? L"MTP MoveObject is not supported by this device."
                         : L"MTP CopyObject is not supported by this device."
                };
                g_Info.Message(g_Info.ModuleNumber, FMSG_MB_OK | FMSG_WARNING, nullptr, msg, 1, 0);
            }
            return true;
        }
    }

    g_Info.Control(PANEL_ACTIVE, FCTL_CLEARSELECTION, 0, 0);
    g_Info.Control(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, 0);
    g_Info.Control(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, 0);
    g_Info.Control(PANEL_PASSIVE, FCTL_UPDATEPANEL, 0, 0);
    g_Info.Control(PANEL_PASSIVE, FCTL_REDRAWPANEL, 0, 0);
    DBG("CrossPanelCopyMoveSameDevice success op=%s", move ? "move" : "copy");
    return true;
}

bool MTPPlugin::EnterSelectedItem() {
    std::string token;
    if (!GetSelectedPanelUserData(token)) {
        DBG("EnterSelectedItem no selected token");
        return false;
    }
    if (token.rfind("DEV|", 0) != 0 && token.rfind("STO|", 0) != 0 && token.rfind("OBJ|", 0) != 0) {
        DBG("EnterSelectedItem invalid token=%s", token.c_str());
        return false;
    }
    DBG("EnterSelectedItem token=%s", token.c_str());
    return EnterByToken(token);
}

bool MTPPlugin::EnterByToken(const std::string& token) {
    DBG("EnterByToken token=%s mode=%d", token.c_str(), static_cast<int>(_view_mode));

    if (token.rfind("DEV|", 0) == 0) {
        const std::string key = token.substr(4);
        DBG("EnterByToken device key=%s", key.c_str());
        if (!EnsureConnected(key)) {
            DBG("EnterByToken device connect failed key=%s", key.c_str());
            const wchar_t* msg[] = {L"Failed to connect to selected device."};
            g_Info.Message(g_Info.ModuleNumber, FMSG_MB_OK | FMSG_WARNING, nullptr, msg, 1, 0);
            return false;
        }
        auto it = _device_binds.find(key);
        _current_device_name.clear();
        if (it != _device_binds.end()) {
            _current_device_name = it->second.candidate.product;
            if (!it->second.candidate.serial.empty()) {
                if (!_current_device_name.empty()) {
                    _current_device_name += " ";
                }
                _current_device_name += "[" + it->second.candidate.serial + "]";
            }
        }
        if (_current_device_name.empty()) {
            _current_device_name = "Storages";
        }
        _view_mode = ViewMode::Storages;
        _panel_title = StrMB2Wide(_current_device_name);
        _current_storage_name.clear();
        _dir_stack.clear();
        RefreshPanel();
        return true;
    }

    uint32_t storage = 0;
    if (ParseStorageToken(token, storage)) {
        DBG("EnterByToken storage id=%u", storage);
        for (const auto& kv : _name_token_index) {
            if (kv.second == token) {
                _last_storage_name = kv.first;
                break;
            }
        }
        _last_storage_id = storage;
        _current_storage_name = _last_storage_name.empty() ? ("Storage " + std::to_string(storage)) : _last_storage_name;
        _dir_stack.clear();
        _current_storage_id = storage;
        _current_parent = 0;
        _view_mode = ViewMode::Objects;
        UpdateObjectsPanelTitle();
        RefreshPanel();
        return true;
    }

    uint32_t handle = 0;
    if (ParseObjectToken(token, handle)) {
        DBG("EnterByToken object handle=%u", handle);
        auto st = _backend ? _backend->Stat(handle) : proto::Result<proto::ObjectEntry>{};
        DBG("EnterByToken object stat ok=%d code=%d is_dir=%d storage=%u parent=%u name=%s",
            st.ok ? 1 : 0,
            st.ok ? 0 : static_cast<int>(st.code),
            (st.ok && st.value.is_dir) ? 1 : 0,
            st.ok ? st.value.storage_id : 0,
            st.ok ? st.value.parent : 0,
            st.ok ? st.value.name.c_str() : "");
        if (st.ok && st.value.is_dir) {
            _current_storage_id = st.value.storage_id;
            _current_parent = handle;
            _view_mode = ViewMode::Objects;
            _dir_stack.push_back(st.value.name);
            UpdateObjectsPanelTitle();
            RefreshPanel();
            return true;
        }
    }

    DBG("EnterByToken unhandled token=%s", token.c_str());
    return false;
}

proto::Status MTPPlugin::DownloadRecursive(uint32_t storage_id,
                                           uint32_t handle,
                                           const std::string& local_root,
                                           proto::CancellationToken token,
                                           uint64_t& downloaded,
                                           uint64_t& total) {
    if (mkdir(local_root.c_str(), 0755) != 0 && errno != EEXIST) {
        return proto::Status::Failure(proto::ErrorCode::Io, "mkdir failed");
    }

    auto children = _backend->ListChildren(storage_id, handle);
    if (!children.ok) {
        return proto::Status::Failure(children.code, children.message, children.retryable);
    }

    for (const auto& child : children.value) {
        if (token.IsCancelled()) {
            return proto::Status::Failure(proto::ErrorCode::Cancelled, "Cancelled by user");
        }

        const std::string target = JoinPath(local_root, child.name);
        if (child.is_dir) {
            auto st = DownloadRecursive(child.storage_id, child.handle, target, token, downloaded, total);
            if (!st.ok) {
                return st;
            }
        } else {
            total += child.size;
            auto st = _transfer->Download(child.handle,
                                          target,
                                          [&downloaded](uint64_t done, uint64_t all) {
                                              (void)all;
                                              downloaded += done;
                                          },
                                          token);
            if (!st.ok) {
                return st;
            }
        }
    }

    return proto::OkStatus();
}
