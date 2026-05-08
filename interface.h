#ifndef INTERFACE_H
#define INTERFACE_H

#include <iostream>
#include <vector>
#include <string>
#include <windows.h>

// --- Include both isolated APIs ---
#include "PDTesterAPI/PDTesterAPI.h"       // Contains namespace Passmark
#include "PDTesterPROAPI/PDTesterProAPI.h" // Contains namespace Passmark_Pro

// Dummy callback required by the Connect function
void GlobalPDEventCallback(int EventCode) {}

// --- Unified Structs ---

struct UnifiedStats {
    uint16_t measuredVoltage_mV;
    uint16_t measuredCurrent_mA;
};

struct UnifiedProfileObj {
    uint16_t MinVoltage;
    uint16_t MaxVoltage;
    uint16_t MaxCurrent;
    int ProfileType;
    int SubType;
};

struct UnifiedCapabilities {
    int NumObjects;
    std::vector<UnifiedProfileObj> Objects;
};

// Replaces the massive parameter list in GetConnectionStatus
struct UnifiedConnectionStatus {
    uint8_t profile_index;
    // We can add the other 13 variables here if you ever need them, 
    // but your OCP recovery currently only evaluates profile_index!
};

class USB_Tester {
public:
    virtual ~USB_Tester() = default;

    // Core Control
    virtual bool Connect(const char* port) = 0;
    virtual void Disconnect() = 0;
    virtual void SetVoltage(int profileIndex, uint16_t voltage_mV) = 0;
    virtual void SetLoad(uint16_t load_mA, uint16_t speed_ms) = 0;
    virtual void SetUsbConnection(uint8_t port, bool isConnected) = 0;

    // Data Retrieval
    virtual bool GetCapabilities(UnifiedCapabilities& caps) = 0;
    virtual bool GetStats(UnifiedStats& stats) = 0;
    virtual bool GetConnectionStatus(UnifiedConnectionStatus& status) = 0;

    // Configuration Abstraction (Replaces your direct tester.CurrentLimitType logic)
    virtual void SetCurrentLimitEnforcement(bool forceLimit, uint16_t maxCurr_mA) = 0;
};

class PDTester_Adapter : public USB_Tester {
private:
    Passmark::PDTester hw;

public:
    bool Connect(const char* port) override {
        return hw.Connect(const_cast<char*>(port), GlobalPDEventCallback);
    }

    void Disconnect() override { hw.Disconnect(); }

    void SetVoltage(int profileIndex, uint16_t voltage_mV) override {
        hw.SetVoltage(profileIndex, voltage_mV);
    }

    void SetLoad(uint16_t load_mA, uint16_t speed_ms) override {
        hw.SetLoad(load_mA);
    }

    void SetUsbConnection(uint8_t port, bool isConnected) override {
        hw.SetUsbConnection(isConnected);
    }

    bool GetStats(UnifiedStats& stats) override {
        UCHAR temp;
        UINT16 mVolt, mVoltSrc, sCurr, mCurr, mCurrSrc;
        if (hw.GetStatistics(&temp, &mVolt, &mVoltSrc, &sCurr, &mCurr, &mCurrSrc)) {
            stats.measuredVoltage_mV = mVolt;
            stats.measuredCurrent_mA = mCurr;
            return true;
        }
        return false;
    }

    bool GetCapabilities(UnifiedCapabilities& caps) override {
        Passmark::USBPD_Capabilities_TypeDef rawCaps;
        if (!hw.GetCapabilities(&rawCaps)) return false;

        caps.NumObjects = rawCaps.NumObjects;
        caps.Objects.clear();
        for (int i = 0; i < rawCaps.NumObjects; i++) {
            UnifiedProfileObj obj;
            obj.MinVoltage = rawCaps.Object[i].MinVoltage;
            obj.MaxVoltage = rawCaps.Object[i].MaxVoltage;
            obj.MaxCurrent = rawCaps.Object[i].MaxCurrent;
            obj.ProfileType = rawCaps.Object[i].Profile.Type;
            obj.SubType = rawCaps.Object[i].Profile.SubType;
            caps.Objects.push_back(obj);
        }
        return true;
    }

    bool GetConnectionStatus(UnifiedConnectionStatus& status) override {
        Passmark::USB_ConnectionStatus_t p_status;
        uint8_t p_idx, p_sub;
        Passmark::PROFILE_TypeDef prof;
        uint16_t v, mc;
        uint32_t mp;

        bool success = hw.GetConnectionStatus(&p_status, &p_idx, &prof, &p_sub, &v, &mc, &mp);

        if (success) {
            status.profile_index = p_idx;
        }
        return success;
    }

    void SetCurrentLimitEnforcement(bool forceLimit, uint16_t maxCurr_mA) override {
        hw.GetConfig();
        if (forceLimit) {
            hw.CurrentLimitType = Passmark::FORCE_LIMIT;
            hw.MaxCurrent = maxCurr_mA;
        }
        else {
            hw.CurrentLimitType = Passmark::ENFORCE_LIMITS;
        }
        hw.SetConfig();
    }
};

class PDTesterPro_Adapter : public USB_Tester {
private:
    Passmark_Pro::PDTester hw;

public:
    bool Connect(const char* port) override {
        return hw.Connect(const_cast<char*>(port), GlobalPDEventCallback);
    }

    void Disconnect() override { hw.Disconnect(); }

    void SetVoltage(int profileIndex, uint16_t voltage_mV) override {
        hw.SetVoltage(profileIndex, voltage_mV);
    }

    void SetLoad(uint16_t load_mA, uint16_t speed_ms) override {
        hw.SetLoad(load_mA, speed_ms);
    }

    void SetUsbConnection(uint8_t port, bool isConnected) override {
        hw.SetUsbConnection(port, isConnected);
    }

    bool GetStats(UnifiedStats& stats) override {
        UCHAR temp;
        UINT16 mVolt, mVoltSrc, sCurr, mCurr, mCurrSrc;
        if (hw.GetStatistics(&temp, &mVolt, &mVoltSrc, &sCurr, &mCurr, &mCurrSrc)) {
            stats.measuredVoltage_mV = mVolt;
            stats.measuredCurrent_mA = mCurr;
            return true;
        }
        return false;
    }

    bool GetCapabilities(UnifiedCapabilities& caps) override {
        Passmark_Pro::USBPD_Capabilities_TypeDef rawCaps;
        if (!hw.GetCapabilities(&rawCaps)) return false;

        caps.NumObjects = rawCaps.NumObjects;
        caps.Objects.clear();
        for (int i = 0; i < rawCaps.NumObjects; i++) {
            UnifiedProfileObj obj;
            obj.MinVoltage = rawCaps.Object[i].MinVoltage;
            obj.MaxVoltage = rawCaps.Object[i].MaxVoltage;
            obj.MaxCurrent = rawCaps.Object[i].MaxCurrent;
            obj.ProfileType = rawCaps.Object[i].Profile.Type;
            obj.SubType = rawCaps.Object[i].Profile.SubType;
            caps.Objects.push_back(obj);
        }
        return true;
    }

    bool GetConnectionStatus(UnifiedConnectionStatus& status) override {
        Passmark_Pro::USB_ConnectionStatus_t p_status, s_p_status;
        uint8_t p_idx, p_sub, s_p_idx, s_p_sub, s_t_idx;
        Passmark_Pro::PROFILE_TypeDef prof, s_prof;
        uint16_t v, mc, s_v, s_mc, s_rc;

        bool success = hw.GetConnectionStatus(&p_status, &p_idx, &prof, &p_sub, &v, &mc,
            &s_p_status, &s_p_idx, &s_prof, &s_p_sub, &s_v, &s_mc, &s_rc, &s_t_idx);

        if (success) {
            status.profile_index = p_idx;
        }
        return success;
    }

    void SetCurrentLimitEnforcement(bool forceLimit, uint16_t maxCurr_mA) override {
        hw.GetConfig();
        if (forceLimit) {
            hw.CurrentLimitType = Passmark_Pro::FORCE_LIMIT;
            hw.MaxCurrent = maxCurr_mA;
        }
        else {
            hw.CurrentLimitType = Passmark_Pro::ENFORCE_LIMITS;
        }
        hw.SetConfigVolatile();
    }
};

#endif