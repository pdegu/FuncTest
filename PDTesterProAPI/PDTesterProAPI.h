#ifndef PASSMARK_PDTESTERPRO_H
#define PASSMARK_PDTESTERPRO_H

#include <stdio.h>
#include <windows.h>
#include "tserial_event_p.h"

// API v1.8

// Change log

// V1.7
// - Changed LATEST_FW to 24

// V1.8
// - Added Source Port Override Voltage command

namespace Passmark_Pro {

#define MAX_NUM_TESTERS			32
#define MAX_SERIAL_LENGTH		16
#define USBPD_MAX_NB_PDO        (7U)              /*!< Maximum number of supported Power Data Objects: fix by the Specification */
#define MAX_PROFILES			24

#define COM_PACKET_ARRIVED		0
#define COM_PACKET_SENT			1
#define COM_SIGNAL_NBR			2  

#define PDAPI_EVENT_PORT_ATTACHED		1
#define PDAPI_EVENT_PORT_DETACHED		2
#define PDAPI_EVENT_PROFILE_CHANGED		3
#define PDAPI_EVENT_NEW_CAPABILITY		4
#define PDAPI_EVENT_SRC_PORT_ATTACHED	5
#define PDAPI_EVENT_SRC_PORT_DETACHED	6
#define PDAPI_EVENT_SRC_PROFILE_CHANGED	7

	typedef void(*type_EventCallBack) (int EventCode);

	typedef enum
	{
		USB_STATUS_NOT_CONNECTED = 0,
		USB_STATUS_CONNECTED
	} USB_ConnectionStatus_t;

	typedef enum
	{
		PROFILE_UNKNOWN = 0,
		PROFILE_LEGACY,
		PROFILE_PTY,
		PROFILE_BC,
		PROFILE_QC,
		PROFILE_UC,
		PROFILE_PD,
		PROFILE_EPR
	}PROFILE_TypeDef;

#define SUBTYPE_USB_DEFAULT		0

	enum
	{
		SUBTYPE_QC1,
		SUBTYPE_QC2,
		SUBTYPE_QC3
	};

	enum
	{
		SUBTYPE_BC_SDP = 0,
		SUBTYPE_BC_CDP,
		SUBTYPE_BC_DCP,
	};

	enum
	{
		SUBTYPE_UC_1_5A = 0,
		SUBTYPE_UC_3A
	};

	enum
	{
		SUBTYPE_PD_FIX = 0,
		SUBTYPE_PD_BAT,
		SUBTYPE_PD_VAR,
		SUBTYPE_PD_APDO,
		SUBTYPE_PD_AVS
	};

	enum
	{
		SUBTYPE_PTY_APPLE_0_5A = 0,
		SUBTYPE_PTY_APPLE_1A,
		SUBTYPE_PTY_APPLE_2_1A,
		SUBTYPE_PTY_APPLE_2_4A
	};




	typedef struct ProfileInfo {
		UINT16 Index : 4;
		UINT16 Type : 4;
		UINT16 SubType : 4;
		UINT16 PDOIndex : 4;
	}ProfileInfo_TypeDef;

	typedef struct
	{
		UINT8 ProfileIndex;
		UINT16 Voltage;
		UINT16 Load;
	}DefaultSetting_TypeDef;

	typedef struct
	{
		ProfileInfo_TypeDef 	Profile;
		UINT16 	MinVoltage;
		UINT16 	MaxVoltage;
		UINT16	MaxCurrent;
	}USBPD_Object_TypeDef;

	typedef struct
	{
		BYTE	NumObjects;
		USBPD_Object_TypeDef Object[MAX_PROFILES];
	}USBPD_Capabilities_TypeDef;

	// PDMsgType number structure
	// Bit 0 - 4: Message type number
	// After Bit 5 = 0: Control msg
	// After Bit 5 = 1: Data msg
	// After Bit 5 = 2: Extended msg
	// After Bit 5 = 3: Other msg
	typedef enum
	{
		// Control message types							// Counter + Should be used (e.g. source only)
		PD_GoodCRC = (0x01 | (0 << 5)),	// 1
		PD_GotoMin = (0x02 | (0 << 5)),	// 1 NO
		PD_Accept = (0x03 | (0 << 5)),	// 2
		PD_Reject = (0x04 | (0 << 5)),	// 3
		PD_Ping = (0x05 | (0 << 5)),	// 2 NO
		PD_PS_RDY = (0x06 | (0 << 5)),	// 4
		PD_Get_Source_Cap = (0x07 | (0 << 5)),	// 5
		PD_Get_Sink_Cap = (0x08 | (0 << 5)),	// 3 NO
		PD_DR_Swap = (0x09 | (0 << 5)),	// 4 NO
		PD_PR_Swap = (0x0A | (0 << 5)),	// 5 NO
		PD_VCONN_Swap = (0x0B | (0 << 5)),	// 6 NO
		PD_Wait = (0x0C | (0 << 5)),	// 6
		PD_Soft_Reset = (0x0D | (0 << 5)),	// 7
		PD_Data_Reset = (0x0E | (0 << 5)),	// 8
		PD_Data_Reset_Complete = (0x0F | (0 << 5)),	// 9
		PD_Not_Supported = (0x10 | (0 << 5)),	// 10
		PD_Get_Source_Cap_Extended = (0x11 | (0 << 5)),	// 11
		PD_Get_Status = (0x12 | (0 << 5)),	// 12
		PD_FR_Swap = (0x13 | (0 << 5)),	// 7 NO
		PD_Get_PPS_Status = (0x14 | (0 << 5)),	// 13
		PD_Get_Country_Codes = (0x15 | (0 << 5)),	// 14
		PD_Get_Sink_Cap_Extended = (0x16 | (0 << 5)),	// 8 NO
		PD_Get_Source_Info = (0x17 | (0 << 5)),	// 15
		PD_Get_Revision = (0x18 | (0 << 5)),	// 16

		// Data message types
		PD_Source_Capabilities = (0x01 | (1 << 5)),	// 9 NO
		PD_Request = (0x02 | (1 << 5)),	// 17
		PD_BIST = (0x03 | (1 << 5)),	// 18
		PD_Sink_Capabilities = (0x04 | (1 << 5)),	// 19
		PD_Battery_Status = (0x05 | (1 << 5)),	// 20
		PD_Alert = (0x06 | (1 << 5)),	// 21
		PD_Get_Country_Info = (0x07 | (1 << 5)),	// 22
		PD_Enter_USB = (0x08 | (1 << 5)),	// 10 NO
		PD_EPR_Request = (0x09 | (1 << 5)),	// 23
		PD_EPR_Mode = (0x0A | (1 << 5)),	// 24
		PD_Source_Info = (0x0B | (1 << 5)),	// 11 NO
		PD_Revision = (0x0C | (1 << 5)),	// 25
		PD_Vendor_Defined = (0x0F | (1 << 5)),	// 26

		// Extended message types
		PD_Source_Capabilities_Extended = (0x01 | (2 << 5)), // 12 NO
		PD_Status = (0x02 | (2 << 5)),	// 27
		PD_Get_Battery_Cap = (0x03 | (2 << 5)),	// 28
		PD_Get_Battery_Status = (0x04 | (2 << 5)),	// 29
		PD_Battery_Capabilities = (0x05 | (2 << 5)),	// 30
		PD_Get_Manufacturer_Info = (0x06 | (2 << 5)),	// 31
		PD_Manufacturer_Info = (0x07 | (2 << 5)),	// 32
		PD_Security_Request = (0x08 | (2 << 5)),	// 33
		PD_Security_Response = (0x09 | (2 << 5)),	// 34
		PD_Firmware_Update_Request = (0x0A | (2 << 5)),	// 35
		PD_Firmware_Update_Response = (0x0B | (2 << 5)),	// 36
		PD_PPS_Status = (0x0C | (2 << 5)),	// 13 NO
		PD_Country_Info = (0x0D | (2 << 5)),	// 37
		PD_Country_Codes = (0x0E | (2 << 5)),	// 38
		PD_Sink_Capabilities_Extended = (0x0F | (2 << 5)),	// 39
		PD_Extended_Control = (0x10 | (2 << 5)),	// 40
		PD_EPR_Source_Capabilities = (0x11 | (2 << 5)),	// 14 NO
		PD_EPR_Sink_Capabilities = (0x12 | (2 << 5)),	// 41
		PD_Vendor_Defined_Extended = (0x1E | (2 << 5)),	// 42

		// Other message types
		PD_Hard_Reset = (0x00 | (3 << 5)),	// 43
	} PDMsgType_t;

	typedef struct
	{
		UINT8 MessageType;
		bool DataRole;
		bool PowerRole;
		UINT8 SpecRev;
		UINT8 MessageID;
		UINT8 NumDataObj;
		UINT8 Data[32];
		bool Extended;
	} PDMsg;

	typedef enum
	{
		ENFORCE_LIMITS = 0,
		ALLOW_20_PERCENT_OVERCURRENT,
		FORCE_LIMIT
	} CurrentLimit_t;

	typedef enum
	{
		CALIB_INDEX_SRC_VBUS,
		CALIB_INDEX_SRC_IBUS,
		CALIB_INDEX_SINK_VBUS,
		CALIB_INDEX_SINK_IBUS
	}CalibrationChannel_t;


	/* -------------------------------------------------------------------- */
	/* -----------------------------  Tserial  ---------------------------- */
	/* -------------------------------------------------------------------- */
	class PDTester
	{
	private:
		void (*PDAnalyzerCallback)(UINT8* msgRaw) = NULL;

		// TIme to wait for a response
		// In PD Analyzer mode is increased to account for the delay it creates
		unsigned int TIME_OUT_MS = 500;

		BOOL	GetDevInfo();

		// -------------------------------------------------------- //
	protected:
		Tserial_event* com;
		DWORD	NumPackets;
		char	ComRcvBuffer[4096];
		WORD	NumRcvBytes;
		WORD	NumRemaingBytes;
		UCHAR	RcvData[4096];
		WORD	RcvDataLen = 0;
		HANDLE	com_events[COM_SIGNAL_NBR];  // events to wait on
		type_EventCallBack	EventCallback;
		CRITICAL_SECTION	DeviceCritSection;
		UCHAR	RcvDataTmp[4096];

		// ++++++++++++++++++++++++++++++++++++++++++++++
		// .................. EXTERNAL VIEW .............
		// ++++++++++++++++++++++++++++++++++++++++++++++
	public:
		const static UCHAR LATEST_FW = 25;

		BOOL	bConnected;
		UCHAR	HW_Ver;
		UCHAR	HW_SubVer;
		UCHAR	HW_SubRevNumber;
		UCHAR	FW_Ver;
		UCHAR	FW_Ver_major;
		UCHAR	FW_Ver_minor;
		UCHAR	FW_Ver_patch;
		UCHAR	FW_Ver_build;
		UCHAR	DeviceID[12];
		bool	bLoopbackPortEnabled;
		bool	bEstimatePortVoltage;
		WORD	MaxSDPCurrent;
		WORD	MaxUSBCCurrent;
		CurrentLimit_t CurrentLimitType;
		WORD	MaxCurrent;
		WORD	CableResistance;
		WORD	DialLoadSpeedms = 200;
		UCHAR	ApplyDefaultOnSrcCap = 0x00;
		UCHAR	DefaultProfileIndex = 0xFF;
		WORD	DefaultVoltage = 5000;
		WORD	DefaultLoad = 0;
		WORD	OperatingCurrent = 0xFFFF;
		WORD	SinkCapMV = 0xFFFF;
		WORD	SinkCapMA = 0xFFFF;
		bool	bHoldLoadOnVChange = false;

		UCHAR	ProfilePDLimit = 0xFF;
		UCHAR	ProfileEPRLimit = 0xFF;
		UCHAR	ProfileBCLimit = 0xFF;
		UCHAR	ProfileQCLimit = 0xFF;
		bool	bPPSEnabled = true; // Not used anymore
		bool	bDefaultConfigFailOnVolt = false;
		bool	bDefaultConfigFailOnCurr = false;


		PDTester();
		~PDTester();
		void	onSerialEvent(void* object, uint32 event);
		BOOL	Connect(char* port, type_EventCallBack event_callback);
		BOOL	GetConfig(void);
		BOOL	SetConfigPersistent(void);
		BOOL	SetConfigVolatile(void);
		BOOL	GetConnectionStatus(USB_ConnectionStatus_t* port_status, BYTE* profile_index, PROFILE_TypeDef* profile, BYTE* profile_subtype, UINT16* voltage, UINT16* max_current, USB_ConnectionStatus_t* src_port_status, BYTE* src_profile_index, PROFILE_TypeDef* src_profile, BYTE* src_profile_subtype, UINT16* src_voltage, UINT16* src_max_current, UINT16* src_req_current, BYTE* src_type_index);
		BOOL	GetCapabilities(USBPD_Capabilities_TypeDef* SourceCapabilities);
		BOOL	GetStatistics(UCHAR* temp, UINT16* voltage, UINT16* voltage_src, UINT16* set_current, UINT16* current, UINT16* current_src);
		BOOL	SetVoltage(UCHAR index, UINT16 voltage);
		BOOL	SetLoad(UINT16 set_current, UINT16 load_speed_ms);
		BOOL	GetCalibrationData(CalibrationChannel_t channel, BOOL* isCalibrated, int* year, int* month, int* applied1, int* measured1, int* applied2, int* measured2);
		BOOL	SetCalibrationData(CalibrationChannel_t channel, int year, int month, int applied1, int measured1, int applied2, int measured2);
		BOOL	ResetCalibrationData(CalibrationChannel_t channel);
		BOOL	SetUsbConnection(UINT8 port, bool isConnected);
		BOOL	StartPDAnalyzer(void (*callback)(UINT8* msgRaw));
		BOOL	StopPDAnalyzer(void);
		void	Disconnect();
		BOOL	GetSrcConfig(UINT8 SrcType, UINT8* SrcTypeName, UINT8* USBCSubType, UINT8* BCSubType, UINT8* AppleSubType,
			UINT8* PDNumProfiles, UINT8* PDSubType, UINT16* PDMinmV, UINT16* PDMaxmV, UINT16* PDMaxmA);
		BOOL	SetSrcConfig(UINT8 SrcType, UINT8* SrcTypeName, UINT8* USBCSubType, UINT8* BCSubType, UINT8* AppleSubType,
			UINT8* PDNumProfiles, UINT8* PDSubType, UINT16* PDMinmV, UINT16* PDMaxmV, UINT16* PDMaxmA);
		BOOL	GetSrcTypes(UINT8* SrcType, UINT8* SrcType1Name, UINT8* SrcType2Name, UINT8* SrcType3Name, UINT8* SrcType4Name, UINT8* SrcType5Name);
		BOOL	SelectSrcType(UINT8 SrcType);
		BOOL	SetSrcOverrideMV(UINT16 mV);
		BOOL	RunFFT(bool enable);
		BOOL	GetFFT(INT16* FFT_Amplitudes);

		static PDMsg ParsePDData(UINT8* pd_data);
	};
	/* -------------------------------------------------------------------- */

}

#endif PASSMARK_PDTESTER_H
