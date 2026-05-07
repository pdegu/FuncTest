#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <windows.h> // Required for QueryDosDeviceA

#include "PDTesterAPI.h"

// --- Configuration Constants ---
const uint16_t CURRENT_STEP_MA = 50;
const uint16_t DWELL_TIME_MS = 1000;
const uint16_t DIAL_SPEED_MS = 100; // How fast the hardware transitions to the new load
const uint16_t VOLTAGE_STEP_MV = 1000; // Step variable profiles by 1V

const char* profileType[] = { "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "LEGACY", "UNKNOWN",
                              "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "APPLE", "APPLE", "APPLE", "APPLE", "APPLE",
                              "SAMSUNG", "BC-SDP", "BC-CDP", "BC-DCP", "UNKNOWN", "UNKNOWN", "UNKNOWN", "QC1", "QC2",
                              "QC3", "UNKNOWN", "UNKNOWN", "UNKNOWN", "USBC", "USBC", "USBC", "UNKNOWN", "UNKNOWN",
                              "UNKNOWN", "PD-FIX", "PD-BAT", "PD-VAR", "PD-PPS", "PD-AVS", "UNKNOWN", "EPR-FIX", "EPR-BAT",
                              "EPR-CAR", "EPR-AVS", "UNKNOWN", "UNKNOWN" };

// Required event callback for the tester (can be left mostly empty for this script)
void PDEventCallback(int EventCode) {
    // We could log connection/disconnection events here if needed
}

std::vector<std::string> ScanForTesters() {
    std::vector<std::string> validPorts;
    char targetPath[255]; // Buffer to hold the hardware path from Windows

    std::cout << "Scanning for connected Passmark testers...\n";

    // Loop through all possible Windows COM ports
    for (int i = 1; i < 256; i++) {
        std::string comName = "COM" + std::to_string(i);

        // QueryDosDeviceA quickly checks if the COM port exists in the Windows registry
        DWORD res = QueryDosDeviceA(comName.c_str(), targetPath, sizeof(targetPath));

        if (res != 0) {
            // The port exists! Let's ping it to see if it's our tester.
            PDTester tempTester;

            // Connect() automatically verifies the hardware by requesting device info
            if (tempTester.Connect(const_cast<char*>(comName.c_str()), PDEventCallback)) {
                std::cout << "  -> Found tester on " << comName
                    << " (FW: " << (int)tempTester.FW_Ver_major << "."
                    << (int)tempTester.FW_Ver_minor << ")\n";

                validPorts.push_back(comName);
                tempTester.Disconnect(); // Free the port so the main script can use it
            }
        }
    }

    return validPorts;
}

bool WaitForVoltageSettling(PDTester& tester, uint16_t targetVolt_mV) {
    const uint16_t VOLTAGE_TOLERANCE_MV = 500; // Acceptable +/- range for target voltage
    const int SETTLE_TIMEOUT_MS = 5000; // Max time to wait for voltage to settle
    const int POLL_INTERVAL_MS = 100; // How often to poll the tester

    std::cout << "  -> Waiting for voltage to settle near " << targetVolt_mV << "mV...\n";
    auto startTime = std::chrono::steady_clock::now();

    int readings = 0; // Required number of consecutive readings within tolerance
    while (true) {
        UCHAR temp;
        UINT16 measVolt, measVoltSrc, setCurr, measCurr, measCurrSrc;

        // Poll the hardware
        if (tester.GetStatistics(&temp, &measVolt, &measVoltSrc, &setCurr, &measCurr, &measCurrSrc)) {
            if (std::abs((int)measVolt - (int)targetVolt_mV) <= VOLTAGE_TOLERANCE_MV) {
                readings++;
            }
        }

        // Consider voltage settled if 3 consecutive readings are within tolerance
        if (readings >= 3) {
            std::cout << "  -> Voltage settled at " << measVolt << "mV.\n";
            return true;
        }

        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

        if (elapsed >= SETTLE_TIMEOUT_MS) {
            std::cerr << "  -> ERROR: Voltage failed to settle within timeout!\n";
            return false; // Timeout reached
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

void RecoverFromOCP(PDTester& tester, uint16_t defaultVolt_mV) {
    const uint8_t PORT = 0x01; // Set port to 0x01 for sink
    const uint16_t LATCH_TIME_MS = 3000;
    const int MAX_RETRY_ATTEMPTS = 3;

    // Drop the tester's load request back to 0 before we reboot the charger
    tester.SetLoad(0, DIAL_SPEED_MS);

    // Disconnect to clear the latch
    tester.SetUsbConnection(PORT, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(LATCH_TIME_MS));

    // Reconnect to wake the DUT back up
    tester.SetUsbConnection(PORT, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(LATCH_TIME_MS));

    // Attempt to restore DUT to working state
    USB_ConnectionStatus_t port_status, src_port_status;
    uint8_t profile_index, profile_subtype, src_profile_index, src_profile_subtype, src_type_index;
    PROFILE_TypeDef profile, src_profile;
    uint16_t voltage, max_current, src_voltage, src_max_current, src_req_current;

    for (int i = 0; i < MAX_RETRY_ATTEMPTS; i++) {
        tester.SetVoltage(0, defaultVolt_mV);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        tester.GetConnectionStatus(&port_status, &profile_index, &profile, &profile_subtype, &voltage, &max_current,
            &src_port_status, &src_profile_index, &src_profile, &src_profile_subtype, &src_voltage, &src_max_current, &src_req_current, &src_type_index);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (profile_index == 0) {
            std::cout << "  -> DUT reset...\n";
            return;
        }
    }

    // DUT failed to reset
    std::cout << "  -> Failed to reset DUT...\n";
}

int main() {
    // 1. Scan for testers
    std::vector<std::string> availableTesters = ScanForTesters();
    if (availableTesters.empty()) {
        std::cerr << "No Passmark testers found. Please check your USB connection.\n";
        return 1;
    }

    std::string selectedComPort;
    if (availableTesters.size() == 1) {
        selectedComPort = availableTesters[0];
        std::cout << "\nAuto-selecting " << selectedComPort << "\n";
    }
    else {
        std::cout << "\nMultiple testers found. Please enter the COM port to use (e.g., COM3): ";
        std::getline(std::cin, selectedComPort);
        // Can add validation here to ensure user typed a valid port from the list
    }

    // 2. Connect the main tester object
    PDTester tester;
    std::cout << "Connecting to " << selectedComPort << "...\n";
    if (!tester.Connect(const_cast<char*>(selectedComPort.c_str()), PDEventCallback)) {
        std::cerr << "Failed to connect to " << selectedComPort << ".\n";
        return 1;
    }
    std::cout << "Connected successfully!\n\n";

    // 3. Get Capabilities
    USBPD_Capabilities_TypeDef capabilities;
    if (!tester.GetCapabilities(&capabilities)) {
        std::cerr << "Failed to read device capabilities.\n";
        tester.Disconnect();
        return 1;
    }

    std::cout << "--- Available Power Profiles ---\n";
    for (int i = 0; i < capabilities.NumObjects; i++) {
        uint16_t minVolt_mV = capabilities.Object[i].MinVoltage;
        uint16_t maxVolt_mV = capabilities.Object[i].MaxVoltage;
        uint16_t curr_mA = capabilities.Object[i].MaxCurrent;
        std::cout << "[" << i + 1 << "] "
            << profileType[capabilities.Object[i].Profile.Type * 6 + capabilities.Object[i].Profile.SubType]
            << ": ";
        if (minVolt_mV == maxVolt_mV) {
            std::cout << maxVolt_mV << "mV @ " << curr_mA << "mA\n";
        }
        else {
            std::cout << minVolt_mV << "-" << maxVolt_mV << "mV @ " << curr_mA << "mA\n";
        }
    }

    // 4. Prompt User for Selection
    std::cout << "\nEnter profiles to test (e.g., '1', '1,3', or '0' for all): ";
    std::string userInput;
    std::getline(std::cin, userInput);

    std::vector<int> selectedIndices;
    if (userInput == "0" || userInput.empty()) {
        for (int i = 0; i < capabilities.NumObjects; i++) {
            selectedIndices.push_back(i + 1);
        }
    }
    else {
        std::stringstream ss(userInput);
        std::string item;
        while (std::getline(ss, item, ',')) {
            selectedIndices.push_back(std::stoi(item));
        }
    }

    // 5. Setup CSV File
    std::string outputFolder = "Test_Results"; // You can name this whatever you prefer

    // Attempt to create the directory
    CreateDirectoryA(outputFolder.c_str(), NULL);

    std::cout << "Enter base file name: ";
    std::string baseFileName;
    std::getline(std::cin, baseFileName);

    // Generate Timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm timeInfo; // local buffer to hold time data securely << this is thread-safe
    localtime_s(&timeInfo, &now_c);
    std::stringstream timeStream;
    timeStream << std::put_time(&timeInfo, "%Y-%m-%d_%H-%M");

    std::string fullPath = outputFolder + "\\" + baseFileName + "_FuncTest_" + timeStream.str() + ".csv";
    std::ofstream csvFile(fullPath);

    if (!csvFile.is_open()) {
        std::cerr << "Error: Could not create the file " << fullPath << "\n";
        tester.Disconnect();
        return 1;
    }

    std::cout << "Data will be saved to: " << fullPath << "\n";
    csvFile << "Profile_Index,Target_Voltage_mV,Target_Current_mA,Measured_Voltage_mV,Measured_Current_mA\n";

    // 6. Execution Loop
    for (int index : selectedIndices) {
        // Arrays are 0-indexed, user input is 1-indexed
        int actualIndex = index - 1;

        // Grab the boundaries for this specific profile
        uint16_t minVolt_mV = capabilities.Object[actualIndex].MinVoltage;
        uint16_t maxVolt_mV = capabilities.Object[actualIndex].MaxVoltage;
        uint16_t maxCurr_mA = capabilities.Object[actualIndex].MaxCurrent;

        std::cout << "\n--- Testing Profile " << index << ": "
            << profileType[capabilities.Object[actualIndex].Profile.Type * 6 + capabilities.Object[actualIndex].Profile.SubType];
        if (minVolt_mV == maxVolt_mV) {
            std::cout << " (" << maxVolt_mV << "mV / " << maxCurr_mA << "mA max) ---\n";
        }
        else {
            std::cout << " (" << minVolt_mV << "mV to " << maxVolt_mV << "mV / " << maxCurr_mA << "mA max) ---\n";
        }

        auto currentLoop = [&](uint16_t targetVolt_mV, uint16_t finalCurr_mA) {
            for (uint16_t load = 0; load <= finalCurr_mA; load += CURRENT_STEP_MA) {
                // Skip redundant command if load is already at 0mA
                if (load > 0) {
                    tester.SetLoad(load, DIAL_SPEED_MS);
                }

                // Wait for stabilization
                std::this_thread::sleep_for(std::chrono::milliseconds(DWELL_TIME_MS));

                // Measure
                UCHAR temp;
                uint16_t measVolt, measVoltSrc, setCurr, measCurr, measCurrSrc;
                if (tester.GetStatistics(&temp, &measVolt, &measVoltSrc, &setCurr, &measCurr, &measCurrSrc)) {

                    // Print to console
                    std::cout << "Load: " << load << "mA | Measured: " << measVolt << "mV, " << measCurr << "mA\n";

                    // Write to CSV
                    csvFile << index << "," << targetVolt_mV << "," << load << "," << measVolt << "," << measCurr << "\n";

                    // Check if OCP event occurred and exit loop if detected
                    if (load > 0 && (measCurr < 5 || measVolt < 1000)) {
                        std::cout << "  -> ERROR: OCP event detected!\n";
                        std::cout << "  -> Resetting DUT state machine...\n";

                        RecoverFromOCP(tester, capabilities.Object[0].MaxVoltage);

                        // Exit loop
                        break;
                    }
                }
                else {
                    std::cerr << "Failed to read stats at " << load << "mA load.\n";
                }
            }

            // Reset load to zero before outer loop increments voltage
            tester.SetLoad(0, DIAL_SPEED_MS);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            };

        // Outer Loop: Step through the voltage range
        // For fixed profiles, minVolt_mV == maxVolt_mV, so this runs exactly once.
        switch (capabilities.Object[actualIndex].Profile.Type * 6 + capabilities.Object[actualIndex].Profile.SubType) {
        case (PROFILE_QC * 6 + SUBTYPE_QC3):
        {
            std::vector<uint16_t> QC3_VOLTAGES_MV = { 5000, 9000, 12000 };
            // Check if DUT supports higher voltages, e.g., 20000mV
            if (QC3_VOLTAGES_MV.back() < maxVolt_mV) {
                QC3_VOLTAGES_MV.push_back(maxVolt_mV);
            }

            for (uint16_t targetVolt_mV : QC3_VOLTAGES_MV) {
                std::cout << "\n  -> Requesting " << targetVolt_mV << "mV...\n";

                // Request the specific voltage from the profile
                tester.SetVoltage(actualIndex, targetVolt_mV);

                // Ensure voltage is stable before continuing
                bool isStable = WaitForVoltageSettling(tester, targetVolt_mV);

                if (isStable) {
                    // Force max current limit to defined upper limits for QC3
                    uint16_t maxCurr_mA_adjusted = (targetVolt_mV == 5000) ? 3000 : (targetVolt_mV == 9000) ? 2000 : maxCurr_mA;
                    tester.GetConfig();
                    if (maxCurr_mA_adjusted != maxCurr_mA) {
                        std::cout << "  -> Setting current limit to " << maxCurr_mA_adjusted << "mA...\n\n";
                        tester.CurrentLimitType = FORCE_LIMIT;
                        tester.MaxCurrent = maxCurr_mA_adjusted;
                    }
                    else {
                        std::cout << "  -> Restoring default current limit...\n\n";
                        tester.CurrentLimitType = ENFORCE_LIMITS;
                    }
                    tester.SetConfigVolatile();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                    // Inner Loop: Step the load at the current voltage
                    currentLoop(targetVolt_mV, maxCurr_mA_adjusted);
                }
                else {
                    // Voltage failed to settle.
                    std::cout << "  -> Timeout reached. Skipping " << targetVolt_mV << "mV sweep...\n";
                }
            }
            break;
        }
        default:
            for (uint16_t targetVolt_mV = minVolt_mV; targetVolt_mV <= maxVolt_mV; targetVolt_mV += VOLTAGE_STEP_MV) {

                std::cout << "\n  -> Requesting " << targetVolt_mV << "mV...\n";

                // Request the specific voltage from the profile
                tester.SetVoltage(actualIndex, targetVolt_mV);

                // Ensure voltage is stable before continuing
                bool isStable = WaitForVoltageSettling(tester, targetVolt_mV);

                if (isStable) {
                    std::cout << "\n";
                    // Inner Loop: Step the load at the current voltage
                    currentLoop(targetVolt_mV, maxCurr_mA);
                }
                else {
                    // Voltage failed to settle
                    std::cout << "  -> Timeout reached. Skipping " << targetVolt_mV << "mV sweep...\n";
                }

                // Check if final voltage step would be greater than max voltage and adjust accordingly
                if (targetVolt_mV < maxVolt_mV && targetVolt_mV + VOLTAGE_STEP_MV > maxVolt_mV) {
                    targetVolt_mV -= targetVolt_mV + VOLTAGE_STEP_MV - maxVolt_mV;
                }
            }
            break;
        }

        // Reset load to 0 before switching to the next profile
        std::cout << "Profile complete. Resetting load to 0mA...\n";
        tester.SetLoad(0, DIAL_SPEED_MS);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }

    // 7. Teardown
    std::cout << "\nTest complete. Disconnecting...\n";
    csvFile.close();
    tester.SetVoltage(0, capabilities.Object[0].MaxVoltage);
    tester.Disconnect();
    return 0;
}