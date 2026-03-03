#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "farplug-wide.h"

#include "backend/BackendRouter.h"
#include "backend/IProtocolSession.h"
#include "mtp/TransferManager.h"

extern PluginStartupInfo g_Info;
extern FarStandardFunctions g_FSF;

class MTPPlugin {
public:
    MTPPlugin(const wchar_t* path = nullptr, bool path_is_standalone_config = false, int op_mode = 0);
    ~MTPPlugin();

    int GetFindData(PluginPanelItem** panel_items, int* items_number, int op_mode);
    void FreeFindData(PluginPanelItem* panel_items, int items_number);
    void GetOpenPluginInfo(OpenPluginInfo* info);
    int SetDirectory(const wchar_t* dir, int op_mode);
    int ProcessKey(int key, unsigned int control_state);
    int ProcessEvent(int event, void* param);

    int MakeDirectory(const wchar_t** name, int op_mode);
    int DeleteFiles(PluginPanelItem* panel_item, int items_number, int op_mode);
    int GetFiles(PluginPanelItem* panel_item, int items_number, int move, const wchar_t** dest_path, int op_mode);
    int PutFiles(PluginPanelItem* panel_item, int items_number, int move, const wchar_t* src_path, int op_mode);
    int Execute(PluginPanelItem* panel_item, int items_number, int op_mode);

    static PluginStartupInfo* GetInfo();

private:
    enum class ViewMode {
        Devices,
        Storages,
        Objects,
    };

    struct DeviceBind {
        proto::UsbDeviceCandidate candidate;
        proto::BackendKind kind = proto::BackendKind::Unknown;
    };

    PluginPanelItem MakePanelItem(const std::string& name,
                                  bool is_dir,
                                  uint64_t size,
                                  uint64_t mtime_epoch,
                                  uint32_t object_id,
                                  uint32_t storage_id,
                                  uint32_t packed_device_id,
                                  const std::string& description = std::string(),
                                  uint64_t ctime_epoch = 0,
                                  uint32_t file_attributes = 0,
                                  uint32_t unix_mode = 0) const;
    PluginPanelItem MakeObjectPanelItem(const proto::ObjectEntry& entry,
                                        uint32_t packed_device_id) const;
    static std::string BuildObjectDescription(const proto::ObjectEntry& entry);
    void UpdateObjectsPanelTitle();

    bool GetSelectedPanelUserData(std::string& out) const;
    bool GetSelectedPanelFileName(std::string& out) const;
    bool EnsureConnected(const std::string& device_key);

    int ListDevices(PluginPanelItem** panel_items, int* items_number);
    int ListStorages(PluginPanelItem** panel_items, int* items_number);
    int ListObjects(PluginPanelItem** panel_items, int* items_number);

    bool ParseStorageToken(const std::string& token, uint32_t& storage_id) const;
    bool ParseObjectToken(const std::string& token, uint32_t& handle) const;
    bool ResolvePanelToken(const PluginPanelItem& item, std::string& token) const;
    bool EnterByToken(const std::string& token);

    int MapErrorToErrno(const proto::Status& st) const;
    int MapErrorToErrno(proto::ErrorCode code) const;
    void SetErrorFromStatus(const proto::Status& st) const;
    bool PromptInput(const wchar_t* title,
                     const wchar_t* prompt,
                     const wchar_t* history_name,
                     const std::string& initial_value,
                     std::string& out) const;
    void RefreshPanel() const;
    bool RenameSelectedItem();
    bool EnterSelectedItem();
    bool ExecuteSelected(int op_mode);
    bool CrossPanelCopyMoveSameDevice(bool move);

    proto::Status DownloadRecursive(uint32_t storage_id,
                                    uint32_t handle,
                                    const std::string& local_root,
                                    proto::CancellationToken token,
                                    uint64_t& downloaded,
                                    uint64_t& total);

    std::wstring _standalone_config;
    std::wstring _panel_title;

    ViewMode _view_mode = ViewMode::Devices;
    std::string _current_device_key;
    uint32_t _current_storage_id = 0;
    uint32_t _current_parent = 0;
    std::string _last_storage_name;
    uint32_t _last_storage_id = 0;
    std::string _current_device_name;
    std::string _current_storage_name;
    std::vector<std::string> _dir_stack;

    std::unordered_map<std::string, DeviceBind> _device_binds;
    std::unordered_map<std::string, std::string> _name_token_index;

    std::shared_ptr<proto::IProtocolSession> _backend;
    std::unique_ptr<proto::TransferManager> _transfer;
    proto::BackendRouter _router;
};
