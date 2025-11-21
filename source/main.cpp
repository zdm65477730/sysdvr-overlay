#define TESLA_INIT_IMPL // If you have more than one file using the tesla header, only define this in the main one
#include <tesla.hpp>    // The Tesla Header

//This is a version for the SysDVR Config app protocol, it's not shown anywhere and not related to the major version
#define SYSDVR_VERSION_MIN 5
#define SYSDVR_VERSION_MAX 17
#define TYPE_MODE_USB 1
#define TYPE_MODE_TCP 2
#define TYPE_MODE_RTSP 4
#define TYPE_MODE_NULL 3
#define TYPE_MODE_SWITCHING 999998
#define TYPE_MODE_ERROR 999999

#define CMD_GET_VER 100
#define CMD_GET_MODE 101
#define CMD_RESET_DISPLAY 103

#define MODE_TO_CMD_SET(x) x
#define UPDATE_INTERVALL 30

using namespace tsl;

bool isServiceRunning(const char *serviceName) {
    u8 tmp = 0;
    SmServiceName service_name = smEncodeName(serviceName);
    Result rc;
    if(hosversionAtLeast(12, 0, 0)){
        rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, service_name, tmp);
    } else {
        rc = serviceDispatchInOut(smGetServiceSession(), 65100, service_name, tmp);
    }
    if (R_SUCCEEDED(rc) && tmp & 1)
        return true;
    else
        return false;
}

long gethostid(void) {
    u32 id = 0x7f000001;

    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_SUCCEEDED(rc)) {
        rc = nifmGetCurrentIpAddress(&id);
        nifmExit();
    }

    return id;
}

class DvrOverlay : public tsl::Gui {
private:
    Service* dvrService;
    bool gotService = false;
    u32 version, ipAddress = 0;
    u32 targetMode = 0;
    std::string modeString;
    std::string versionString;
    char ipString[20];
    u32 statusColor = 0;
public:
    DvrOverlay(Service* dvrService, bool gotService) {
        this->gotService = gotService;
        this->dvrService = dvrService;
    }

    // Called when this Gui gets loaded to create the UI
    // Allocate all elements on the heap. libtesla will make sure to clean them up when not needed anymore
    virtual tsl::elm::Element* createUI() override {
        // A OverlayFrame is the base element every overlay consists of. This will draw the default Title and Subtitle.
        // If you need more information in the header or want to change it's look, use a HeaderOverlayFrame.
        auto frame = new tsl::elm::OverlayFrame("PluginName"_tr, VERSION);

        // A list that can contain sub elements and handles scrolling
        auto list = new tsl::elm::List();

        if(!gotService) {   
            list->addItem(getErrorDrawer("SetupSysDvrServiceFailedDvrOverlayErrorDrawerText"_tr), getErrorDrawerSize());
            frame->setContent(list);
            return frame;
        }
        
        sysDvrGetVersion(&version);
        versionString = std::to_string(version);

        if(version>SYSDVR_VERSION_MAX ||version<SYSDVR_VERSION_MIN) {
            list->addItem(getErrorDrawer("UnkownSysDvrConfigAPIVersionDvrOverlayErrorDrawerText"_tr + std::to_string(version)
                + "SupportedConfigAPIVersionSysDvrConfigAPIDvrOverlayErrorDrawerText"_tr + std::to_string(SYSDVR_VERSION_MIN) + " - v" + std::to_string(SYSDVR_VERSION_MAX)), getErrorDrawerSize());
            frame->setContent(list);
            return frame;
        }

        if (gotService) {
            sysDvrGetMode(&targetMode);
            updateMode();
        }

        ipAddress = gethostid();
        updateIP();

        auto infodrawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("InfoDvrOverlayCustomDrawerText"_tr.c_str(), false, x + 3, y + 16, 20, renderer->a(0xFFFF));
            renderer->drawString("ModeDvrOverlayCustomDrawerText"_tr.c_str(), false, x + 3, y + 40, 16, renderer->a(0xFFFF));
            renderer->drawString("IPAddressDvrOverlayCustomDrawerText"_tr.c_str(), false, x + 3, y + 60, 16, renderer->a(0xFFFF));
            renderer->drawString("IPCVersionDvrOverlayCustomDrawerText"_tr.c_str(), false, x + 3, y + 80, 16, renderer->a(0xFFFF));
            renderer->drawCircle(x + 116, y + 35, 5, true, renderer->a(statusColor));
            renderer->drawString(modeString.c_str(), false, x + 130, y + 40, 16, renderer->a(0xFFFF));

            renderer->drawString(ipString, false, x + 110, y + 60, 16, renderer->a(0xFFFF));

            renderer->drawString(versionString.c_str(), false, x + 110, y + 80, 16, renderer->a(0xFFFF));
        });
        list->addItem(infodrawer, 85);

        // List Items
        list->addItem(new tsl::elm::CategoryHeader("ChangeModeDvrOverlayCategoryHeaderText"_tr));

        auto *offItem = new tsl::elm::ListItem("OffModeDvrOverlayListItemText"_tr);
        offItem->setClickListener(getModeLambda(TYPE_MODE_NULL));
        list->addItem(offItem);

        auto *usbModeItem = new tsl::elm::ListItem("USBModeDvrOverlayListItemText"_tr);
        usbModeItem->setClickListener(getModeLambda(TYPE_MODE_USB));
        list->addItem(usbModeItem);

        auto *tcpModeItem = new tsl::elm::ListItem("TCPModeDvrOverlayListItemText"_tr);
        tcpModeItem->setClickListener(getModeLambda(TYPE_MODE_TCP));
        list->addItem(tcpModeItem);

        auto *rtspModeItem = new tsl::elm::ListItem("RTSPModeDvrOverlayListItemText"_tr);
        rtspModeItem->setClickListener(getModeLambda(TYPE_MODE_RTSP));
        list->addItem(rtspModeItem);

        // Add the list to the frame for it to be drawn
        frame->setContent(list);

        // Return the frame to have it become the top level element of this Gui
        return frame;
    }

    tsl::elm::CustomDrawer* getErrorDrawer(std::string message1) {
        return new tsl::elm::CustomDrawer([message1](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString(message1.c_str(), false, x + 3, y + 15, 20, renderer->a(0xF22F));
        });
    }

    int getErrorDrawerSize() {
        return 50;
    }

    std::function<bool(u64 keys)> getModeLambda(u32 mode) {
        return [this,mode](u64 keys) {
            if (keys & HidNpadButton_A) {
                ipAddress = gethostid();
                updateIP();
                if (gotService) {
                    sysDVRRequestModeChange(mode);
                } else {
                    return false;
                }
                return true;
            }
            return false;
        };
    }

    // Called once every frame to update values
    int currentFrame = 0;
    virtual void update() override {
        currentFrame++;
        //only check for dvr mode and ip cahnges every 30 fps, so 0,5-1 sec
        if(currentFrame >= UPDATE_INTERVALL) {
            currentFrame = 0;
            updateMode();
            updateIP();
        }
    }

    void updateMode() {
        modeString = getModeString(targetMode);
        if(targetMode == TYPE_MODE_SWITCHING){
            statusColor = 0xF088;
        } else if(targetMode == TYPE_MODE_ERROR){
            statusColor = 0xF22F;
        } else if(targetMode == TYPE_MODE_NULL){
            statusColor = 0xF333;
        } else {
            statusColor = 0xF0F0;
        }
    }

    void updateIP() {
        snprintf(ipString, sizeof(ipString)-1, "%u.%u.%u.%u", ipAddress&0xFF, (ipAddress>>8)&0xFF, (ipAddress>>16)&0xFF, (ipAddress>>24)&0xFF);
    }

    // Called once every frame to handle inputs not handled by other UI elements
    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        return false;   // Return true here to signal the inputs have been consumed
    }

    std::string getModeString(u32 mode) {
        switch(mode){
            case TYPE_MODE_USB:
                return "USBModeDvrOverlayListItemText"_tr;
            case TYPE_MODE_TCP:
                return "TCPModeDvrOverlayListItemText"_tr;
            case TYPE_MODE_RTSP:
                return "RTSPModeDvrOverlayListItemText"_tr;
            case TYPE_MODE_NULL:
                return "OffModeDvrOverlayListItemText"_tr;
            case TYPE_MODE_SWITCHING:
                return "SwitchingModeDvrOverlayListItemText"_tr;
            case TYPE_MODE_ERROR:
                return "ErrorModeDvrOverlayListItemText"_tr;
            default:
                return "UnkownModeDvrOverlayListItemText"_tr;
        }
    }

    Result sysDvrGetVersion(u32* out_ver) {
        u32 val;
        Result rc = serviceDispatchOut(dvrService, CMD_GET_VER, val);
        if (R_SUCCEEDED(rc))
            *out_ver = val;
        return rc;
    }

    Result sysDvrGetMode(u32* out_mode) {
        u32 val;
        Result rc = serviceDispatchOut(dvrService, CMD_GET_MODE, val);
        if (R_SUCCEEDED(rc))
            *out_mode = val;
        return rc;
    }

    void sysDVRRequestModeChange(u32 command) {
        targetMode = TYPE_MODE_SWITCHING;
        updateMode();
        serviceDispatch(dvrService, MODE_TO_CMD_SET(command));
        //svcSleepThread(2000'000'000);
        //close and reinit sysdvr service, to directly apply the new mode.
        //serviceClose(dvrService);
        //svcSleepThread(100'000'000);
        //gotService = R_SUCCEEDED(smGetService(dvrService, "sysdvr"));
        svcSleepThread(500'000'000);
        targetMode = command;

        //u32 curMode;
        //if (R_SUCCEEDED(sysDvrGetMode(&curMode))) {
        //    if (command != curMode)
        //        targetMode = curMode;
        //}
    }
};

class OverlayTest : public tsl::Overlay {
private:
    Service dvr;
    bool gotService = false;
public:
    // libtesla already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys
    virtual void initServices() override {
        std::string jsonStr = R"(
            {
                "PluginName": "Sysdvr",
                "SetupSysDvrServiceFailedDvrOverlayErrorDrawerText": "Failed to setup SysDVR Service!\nIs sysdvr running?",
                "UnkownSysDvrConfigAPIVersionDvrOverlayErrorDrawerText": "Unkown SysDVR Config API: v",
                "SupportedConfigAPIVersionSysDvrConfigAPIDvrOverlayErrorDrawerText": "\nOnly support Config API v",
                "InfoDvrOverlayCustomDrawerText": "Info",
                "ModeDvrOverlayCustomDrawerText": "Mode:",
                "IPAddressDvrOverlayCustomDrawerText": "IP-Address:",
<<<<<<< HEAD
=======
                "IPCVersionDvrOverlayCustomDrawerText": "IPC-Version:",
>>>>>>> Hartie95-master
                "ChangeModeDvrOverlayCategoryHeaderText": "Change Mode",
                "OffModeDvrOverlayListItemText": "OFF",
                "USBModeDvrOverlayListItemText": "USB",
                "TCPModeDvrOverlayListItemText": "TCP",
                "RTSPModeDvrOverlayListItemText": "RTSP",
                "SwitchingModeDvrOverlayListItemText": "Switching",
                "ErrorModeDvrOverlayListItemText": "Error",
                "UnkownModeDvrOverlayListItemText": "Unkown"
            }
        )";
        std::string lanPath = std::string("sdmc:/switch/.overlays/lang/") + APPTITLE + "/";
        fsdevMountSdmc();
        tsl::hlp::doWithSmSession([&lanPath, &jsonStr]{
            tsl::tr::InitTrans(lanPath, jsonStr);
        });
        fsdevUnmountDevice("sdmc");

        smInitialize();
        if(isServiceRunning("sysdvr")) {
            gotService = R_SUCCEEDED(smGetService(&dvr, "sysdvr"));
        }
        //nifmInitialize(NifmServiceType_User);
    }  // Called at the start to initialize all services necessary for this Overlay
    virtual void exitServices() override {
        //nifmExit();
        if(gotService){
            serviceClose(&dvr);
        }
        smExit();
    }  // Callet at the end to clean up all services previously initialized

    virtual void onShow() override {}    // Called before overlay wants to change from invisible to visible state
    virtual void onHide() override {}    // Called before overlay wants to change from visible to invisible state

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<DvrOverlay>(&dvr, gotService);  // Initial Gui to load. It's possible to pass arguments to it's constructor like this
    }
};

int main(int argc, char **argv) {
    return tsl::loop<OverlayTest>(argc, argv);
}