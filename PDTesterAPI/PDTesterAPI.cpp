#include <windows.h>
#include <stdio.h>
#include <process.h>
#include <CommCtrl.h>
#include <winioctl.h>
#include <time.h>
#include <windowsx.h>
#include <cstdlib> // For mbstowcs()

#include <tchar.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <string>
#include <iostream>
#include <vector>
#include <initguid.h>
#include <devpropdef.h>


#include "PDTesterAPI.h"
	
#define GET_DEV_INFO					0x01
#define GET_CONSTAT						0x0A
#define GET_PORT_CAPABILITIES			0x0B
#define GET_STAT						0x0C
#define SET_PORT_VOLTAGE				0x0D
#define	SET_CURRENT						0x10
#define SET_USB_CONNECTION				0x14
#define GET_SRC_CONFIG					0x30
#define SET_SRC_CONFIG					0x31
#define GET_SRC_TYPES					0x32
#define SELECT_SRC_TYPE					0x33
#define SET_SRC_OVERRIDE_MV				0x40
#define	GET_CONFIG						0xE0
#define	SET_CONFIG_VOLATILE				0xE1
#define	SET_CONFIG_PERSISTENT			0xE2
#define SET_CALIB_DATA					0xE3
#define GET_CALIB_DATA					0xE4
#define RESET_CALIB_DATA				0xE5
#define SET_PD_ANALYZER					0xE7
#define RUN_FFT							0xE8
#define GET_FFT							0xE9


#define EVENT_PORT_ATTACHED				0x20
#define EVENT_PORT_DETACHED				0x21
#define EVENT_PROFILE_CHANGED			0x22
#define EVENT_NEW_CAPABILITY			0x23
#define EVENT_PD_MSG_RECV				0x24
#define EVENT_PD_MSG_SENT				0x25
#define EVENT_SRC_PORT_ATTACHED			0x26
#define EVENT_SRC_PORT_DETACHED			0x27
#define EVENT_SRC_PROFILE_CHANGED		0x28


#define	CFG_LOOPBACK_ENABLE			0
#define	CFG_SET_MAX_CURRENT			1
#define	CFG_SDP_MAX_CURRENT			2
#define	CFG_ESTIMATE_VBUS			3
#define	CFG_CABLE_RESISTANCE		4
#define	CFG_DEFAULT_PROFILE_IDX		5
#define	CFG_DEFAULT_VOLTAGE			6
#define	CFG_DEFAULT_LOAD			7
#define	CFG_OPERATING_CURRENT		8
#define	CFG_SINK_CAP				9
#define	CFG_PROFILE_LIMIT			0xA
#define CFG_PPS_ENABLED				0x17
#define	CFG_DEF_CONF_ON_CAP			0x20
#define	CFG_DIAL_LOAD_SPEED			0x21
#define CFG_USBC_MAX_CURRENT		0x22
#define CFG_DEF_FAIL_VOLT			0x23
#define CFG_DEF_FAIL_CURR			0x24

//#define	TIME_OUT_MS					500

#define MAX_MSG_INJECT_LENGTH		50

// Link with SetupAPI.lib
#pragma comment(lib, "setupapi.lib")
// Define the GUID for the Ports class
DEFINE_GUID(GUID_DEVCLASS_PORTS, 0x4d36e978, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);
// Manually define DEVPKEY_Device_BusReportedDeviceDesc
DEFINE_DEVPROPKEY(DEVPKEY_Device_BusReportedDeviceDesc, 0x540b947e, 0x8b40, 0x45bc, 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2, 4);


void SerialEventManager(void* object, void* parent_class, uint32 event)
{
	PDTester *self = static_cast<PDTester*>(parent_class);
	Tserial_event *com;
	com = (Tserial_event *)object;

	self->onSerialEvent(object, event);
}

PDTester::PDTester()
{
	int i;

	RcvDataLen = 0;
	NumRcvBytes = 0;
	NumRemaingBytes = 1;

	// creating Events for the different sources
	for (i = 0; i<COM_SIGNAL_NBR; i++)
		com_events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);

	InitializeCriticalSection(&DeviceCritSection);
	com = new Tserial_event();
	bConnected = FALSE;
	if (com != 0) {
		com->setManager(SerialEventManager, this);
		com->setRxSize(SERIAL_MAX_RX);
	}
}

PDTester::~PDTester()
{
	int i;

	for (i = 0; i<COM_SIGNAL_NBR; i++)         // deleting the events
	{
		if (com_events[i] != INVALID_HANDLE_VALUE)
			CloseHandle(com_events[i]);
		com_events[i] = INVALID_HANDLE_VALUE;
	}
}

BOOL PDTester::Connect(char* port, type_EventCallBack event_callback)
{
	EnterCriticalSection(&DeviceCritSection);

	int len = MultiByteToWideChar(CP_ACP, 0, port, -1, NULL, 0);
	std::wstring widePortName(len, L'\0');
	MultiByteToWideChar(CP_ACP, 0, port, -1, &widePortName[0], len);
	std::wstring comPortName = L"\\\\.\\" + widePortName;

	BOOL status = FALSE;

	//InitializeCriticalSection(&DeviceCritSection);
	EventCallback = event_callback;

	//com = new Tserial_event();
	if (com != 0)
	{
		//com->setManager(SerialEventManager, this);
		//com->setRxSize(SERIAL_MAX_RX);
		status = com->connect((wchar_t *)comPortName.c_str(), 921600, SERIAL_PARITY_NONE, 8, true);
		if (status == 0)
		{
			bConnected = TRUE;
			com->setRxSize(1);
			com->dataHasBeenRead();
			GetDevInfo();
		}
	}

	LeaveCriticalSection(&DeviceCritSection);

	return bConnected;
}

void PDTester::Disconnect()
{
	// Do it before the lock so if anything else wants to try grab the lock
	// before this it will assume nothing is connected and free the lock quicker
	bConnected = FALSE;

	EnterCriticalSection(&DeviceCritSection);

	if (com != 0)
	{
		com->disconnect();
	}

	LeaveCriticalSection(&DeviceCritSection);
}

/* ======================================================== */
/* ===============  OnCharArrival     ===================== */
/* ======================================================== */
void PDTester::onSerialEvent(void *object, uint32 event)
{
	char	*ptr;
	int		size;
	int		i;
	bool	bPacketReceived = 0;
	Tserial_event *com;

	com = (Tserial_event *)object;
	if (com != 0)
	{
		switch (event)
		{
		case  SERIAL_CONNECTED:
			com->dataHasBeenRead();
			com->setRxSize(1);
			break;
		case  SERIAL_DISCONNECTED:
			break;
		case  SERIAL_DATA_SENT:
			SetEvent(com_events[COM_PACKET_SENT]);
			break;
		case  SERIAL_RING:
			break;
		case  SERIAL_CD_ON:
			break;
		case  SERIAL_CD_OFF:
			break;
		case  SERIAL_DATA_ARRIVAL:
			size = com->getDataInSize();
			ptr = com->getDataInBuffer();
			if (size == 0)
			{
				RcvDataLen = 0;
				NumRemaingBytes = 1;
			}
			for (i = 0; i < size; i++)
			{
				if (NumRcvBytes == 0)
				{
					if (*ptr != 0x02)
						RcvDataLen = 0;
					else
						ComRcvBuffer[NumRcvBytes++] = *ptr;
					NumRemaingBytes = 1;
				}
				else if (NumRcvBytes == 1)
				{
					ComRcvBuffer[NumRcvBytes++] = *ptr;
					RcvDataLen = (uint8_t)(*ptr);
					NumRemaingBytes = RcvDataLen + 2;
				}
				else
				{
					NumRemaingBytes--;
					ComRcvBuffer[NumRcvBytes++] = *ptr;
					if (!NumRemaingBytes)
					{
						if (*ptr == 0x03)
						{
							memcpy(RcvDataTmp, &ComRcvBuffer[2], RcvDataLen);
							bPacketReceived = 1;
							NumPackets++;
						}
						else
							RcvDataLen = 0;
						NumRcvBytes = 0;
						NumRemaingBytes = 1;
					}
					
				}
				*ptr++;
			}
			com->setRxSize(NumRemaingBytes);
			com->dataHasBeenRead();
			if (bPacketReceived)
			{
				if (RcvDataTmp[0] == EVENT_PORT_ATTACHED)
					EventCallback(PDAPI_EVENT_PORT_ATTACHED);
				else if (RcvDataTmp[0] == EVENT_PORT_DETACHED)
					EventCallback(PDAPI_EVENT_PORT_DETACHED);
				else if (RcvDataTmp[0] == EVENT_PROFILE_CHANGED)
					EventCallback(PDAPI_EVENT_PROFILE_CHANGED);
				else if (RcvDataTmp[0] == EVENT_SRC_PORT_ATTACHED)
					EventCallback(PDAPI_EVENT_SRC_PORT_ATTACHED);
				else if (RcvDataTmp[0] == EVENT_SRC_PORT_DETACHED)
					EventCallback(PDAPI_EVENT_SRC_PORT_DETACHED);
				else if (RcvDataTmp[0] == EVENT_SRC_PROFILE_CHANGED)
					EventCallback(PDAPI_EVENT_SRC_PROFILE_CHANGED);
				else if (RcvDataTmp[0] == EVENT_NEW_CAPABILITY)
					EventCallback(PDAPI_EVENT_NEW_CAPABILITY);
				else if ((RcvDataTmp[0] == EVENT_PD_MSG_RECV || RcvDataTmp[0] == EVENT_PD_MSG_SENT))
				{
					if (PDAnalyzerCallback != NULL)
					{
						// Copy data to a buffer then call callback function
						UCHAR PDData[36];
						memcpy(PDData, &RcvDataTmp[1], 36);
						PDAnalyzerCallback(PDData);
					}
				}
				else
				{
					memcpy(RcvData, &RcvDataTmp[0], 250);
					SetEvent(com_events[COM_PACKET_ARRIVED]);
				}
			}
		}
	}
}

UCHAR checksum(UCHAR *buf)
{
	UCHAR chk = 0;
	int i;

	int DataLen = buf[1];
	buf[DataLen + 2] = 0;
	for (i = 0; i < DataLen + 2; i++)
		chk ^= buf[i];
	chk ^= 0x03;
	return chk;
}

BOOL PDTester::GetDevInfo()
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x01;
	cBuf[2] = GET_DEV_INFO;
	cBuf[3] = checksum(cBuf);
	cBuf[4] = 0x03;
	com->sendData((char*)cBuf, 5);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == GET_DEV_INFO)
			{
				HW_Ver = RcvData[1];
				FW_Ver_major = RcvData[2];
				FW_Ver_minor = RcvData[3];
				FW_Ver_patch = RcvData[4];
				FW_Ver_build = RcvData[5];
				memcpy(DeviceID, &RcvData[6], 12);
				FW_Ver = FW_Ver_major * 10 + FW_Ver_minor; // Still need FW_Ver to keep compatiability with other software
			}
			else
				retVal = FALSE;
		}
		else
		{
			HW_Ver = 0;
			retVal = FALSE;
		}
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}


BOOL PDTester::GetConfig(void)
{
	UCHAR	cBuf[10];
	long	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	// Get loopback enable
	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = GET_CONFIG;
	cBuf[3] = CFG_LOOPBACK_ENABLE;
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
		{
			bLoopbackPortEnabled = RcvData[1];
		}
		else
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	// Get SDP max current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_SDP_MAX_CURRENT;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				MaxSDPCurrent = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get USBC max current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_USBC_MAX_CURRENT;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				MaxUSBCCurrent = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get upstream vbus estimate config
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_ESTIMATE_VBUS;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				bEstimatePortVoltage = RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get cable resistance
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_CABLE_RESISTANCE;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				CableResistance = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config Dial Load Speed
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DIAL_LOAD_SPEED;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				DialLoadSpeedms = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config apply default config on src capability change
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DEF_CONF_ON_CAP;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				ApplyDefaultOnSrcCap = RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config default profile index
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DEFAULT_PROFILE_IDX;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG)
			{
				DefaultProfileIndex = RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config default voltage
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DEFAULT_VOLTAGE;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
			{
				DefaultVoltage = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config default load
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DEFAULT_LOAD;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
			{
				DefaultLoad = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config operating current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_OPERATING_CURRENT;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
			{
				OperatingCurrent = ((unsigned int)RcvData[2] << 8) + RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get sink capability
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_SINK_CAP;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
			{
				SinkCapMV = ((unsigned int)RcvData[2] << 8) + RcvData[1];
				SinkCapMA = ((unsigned int)RcvData[4] << 8) + RcvData[3];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get config profile limits
	for (int i = 0; i < 4; i++)
	{
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x02;
			cBuf[2] = GET_CONFIG;
			cBuf[3] = CFG_PROFILE_LIMIT + i;

			cBuf[4] = checksum(cBuf);
			cBuf[5] = 0x03;
			com->sendData((char*)cBuf, 6);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
				{
					if (i == 0)
						ProfilePDLimit = RcvData[1];
					else if (i == 1)
						ProfileEPRLimit = RcvData[1];
					else if (i == 2)
						ProfileBCLimit = RcvData[1];
					else if (i == 3)
						ProfileQCLimit = RcvData[1];
				}
				else
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}
	}

	// Get should voltage fail or find closest lower voltage on default config fail
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DEF_FAIL_VOLT;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
			{
				bDefaultConfigFailOnVolt = RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Get should current fail or find closest lower voltage on default config fail
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = GET_CONFIG;
		cBuf[3] = CFG_DEF_FAIL_CURR;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
			{
				bDefaultConfigFailOnCurr = RcvData[1];
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}


	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::SetConfigPersistent(void)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	// Set config loopback enable
	cBuf[0] = 0x02;
	cBuf[1] = 0x03;
	cBuf[2] = SET_CONFIG_PERSISTENT;
	cBuf[3] = CFG_LOOPBACK_ENABLE;
	cBuf[4] = bLoopbackPortEnabled;
	cBuf[5] = checksum(cBuf);
	cBuf[6] = 0x03;
	com->sendData((char*)cBuf, 7);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	Sleep(100);

	// Set config current limit
	if (retVal == TRUE)
	{

		if (CurrentLimitType == FORCE_LIMIT)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x05;
			cBuf[2] = SET_CONFIG_PERSISTENT;
			cBuf[3] = CFG_SET_MAX_CURRENT;
			cBuf[4] = CurrentLimitType;
			cBuf[5] = (UCHAR)MaxCurrent & 0xff;
			cBuf[6] = (UCHAR)((MaxCurrent >> 8) & 0xff);
			cBuf[7] = checksum(cBuf);
			cBuf[8] = 0x03;
			com->sendData((char*)cBuf, 9);
		}
		else
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG_PERSISTENT;
			cBuf[3] = CFG_SET_MAX_CURRENT;
			cBuf[4] = CurrentLimitType;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
		}
		
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config SDP max current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_SDP_MAX_CURRENT;
		cBuf[4] = (UCHAR)MaxSDPCurrent & 0xff;
		cBuf[5] = (UCHAR)(MaxSDPCurrent >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config USBC max current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_USBC_MAX_CURRENT;
		cBuf[4] = (UCHAR)MaxUSBCCurrent & 0xff;
		cBuf[5] = (UCHAR)(MaxUSBCCurrent >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config upstream vbus estimate
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_ESTIMATE_VBUS;
		cBuf[4] = bEstimatePortVoltage;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);
	
	// Set config cable resistance
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_CABLE_RESISTANCE;
		cBuf[4] = (UCHAR)CableResistance & 0xff;
		cBuf[5] = (UCHAR)(CableResistance >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set Dial Load Speed
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DIAL_LOAD_SPEED;
		cBuf[4] = (UCHAR)DialLoadSpeedms & 0xff;
		cBuf[5] = (UCHAR)(DialLoadSpeedms >> 8) & 0xff;;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config apply default configuration on SRC capability change
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DEF_CONF_ON_CAP;
		cBuf[4] = ApplyDefaultOnSrcCap;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config default profile index
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DEFAULT_PROFILE_IDX;
		cBuf[4] = DefaultProfileIndex;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config default voltage
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DEFAULT_VOLTAGE;
		cBuf[4] = (UCHAR)DefaultVoltage & 0xff;
		cBuf[5] = (UCHAR)(DefaultVoltage >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config default load
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DEFAULT_LOAD;
		cBuf[4] = (UCHAR)DefaultLoad & 0xff;
		cBuf[5] = (UCHAR)(DefaultLoad >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config operating current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_OPERATING_CURRENT;
		cBuf[4] = (UCHAR)OperatingCurrent & 0xff;
		cBuf[5] = (UCHAR)(OperatingCurrent >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set sink capability
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x06;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_SINK_CAP;
		cBuf[4] = (UCHAR)SinkCapMV & 0xff;
		cBuf[5] = (UCHAR)(SinkCapMV >> 8) & 0xff;
		cBuf[6] = (UCHAR)SinkCapMA & 0xff;
		cBuf[7] = (UCHAR)(SinkCapMA >> 8) & 0xff;
		cBuf[8] = checksum(cBuf);
		cBuf[9] = 0x03;
		com->sendData((char*)cBuf, 10);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config profile limits
	for (int i = 0; i < 4; i++)
	{
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG_PERSISTENT;
			cBuf[3] = CFG_PROFILE_LIMIT + i;

			UCHAR limit = 0;
			if (i == 0)
				limit = ProfilePDLimit;
			else if (i == 1)
				limit = ProfileEPRLimit;
			else if (i == 2)
				limit = ProfileBCLimit;
			else if (i == 3)
				limit = ProfileQCLimit;

			cBuf[4] = limit;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}
	}

	// Set should current fail or find closest lower voltage on default config fail
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DEF_FAIL_VOLT;
		cBuf[4] = bDefaultConfigFailOnVolt;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set should current fail or find closest lower voltage on default config fail
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_PERSISTENT;
		cBuf[3] = CFG_DEF_FAIL_CURR;
		cBuf[4] = bDefaultConfigFailOnCurr;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}


	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::SetConfigVolatile(void)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	// Set config loopback enable
	cBuf[0] = 0x02;
	cBuf[1] = 0x03;
	cBuf[2] = SET_CONFIG_VOLATILE;
	cBuf[3] = CFG_LOOPBACK_ENABLE;
	cBuf[4] = bLoopbackPortEnabled;
	cBuf[5] = checksum(cBuf);
	cBuf[6] = 0x03;
	com->sendData((char*)cBuf, 7);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	Sleep(100);

	// Set config current limit
	if (retVal == TRUE)
	{

		if (CurrentLimitType == FORCE_LIMIT)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x05;
			cBuf[2] = SET_CONFIG_VOLATILE;
			cBuf[3] = CFG_SET_MAX_CURRENT;
			cBuf[4] = CurrentLimitType;
			cBuf[5] = (UCHAR)MaxCurrent & 0xff;
			cBuf[6] = (UCHAR)((MaxCurrent >> 8) & 0xff);
			cBuf[7] = checksum(cBuf);
			cBuf[8] = 0x03;
			com->sendData((char*)cBuf, 9);
		}
		else
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG_VOLATILE;
			cBuf[3] = CFG_SET_MAX_CURRENT;
			cBuf[4] = CurrentLimitType;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
		}

		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config SDP max current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_SDP_MAX_CURRENT;
		cBuf[4] = (UCHAR)MaxSDPCurrent & 0xff;
		cBuf[5] = (UCHAR)(MaxSDPCurrent >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config USBC max current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_USBC_MAX_CURRENT;
		cBuf[4] = (UCHAR)MaxUSBCCurrent & 0xff;
		cBuf[5] = (UCHAR)(MaxUSBCCurrent >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config upstream vbus estimate
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_ESTIMATE_VBUS;
		cBuf[4] = bEstimatePortVoltage;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	Sleep(100);

	// Set config cable resistance
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_CABLE_RESISTANCE;
		cBuf[4] = (UCHAR)CableResistance & 0xff;
		cBuf[5] = (UCHAR)(CableResistance >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set Dial Load Speed
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DIAL_LOAD_SPEED;
		cBuf[4] = (UCHAR)DialLoadSpeedms & 0xff;
		cBuf[5] = (UCHAR)(DialLoadSpeedms >> 8) & 0xff;;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config apply default configuration on SRC capability change
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DEF_CONF_ON_CAP;
		cBuf[4] = ApplyDefaultOnSrcCap;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_PERSISTENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config default profile index
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DEFAULT_PROFILE_IDX;
		cBuf[4] = DefaultProfileIndex;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config default voltage
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DEFAULT_VOLTAGE;
		cBuf[4] = (UCHAR)DefaultVoltage & 0xff;
		cBuf[5] = (UCHAR)(DefaultVoltage >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config default load
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DEFAULT_LOAD;
		cBuf[4] = (UCHAR)DefaultLoad & 0xff;
		cBuf[5] = (UCHAR)(DefaultLoad >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config operating current
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x04;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_OPERATING_CURRENT;
		cBuf[4] = (UCHAR)OperatingCurrent & 0xff;
		cBuf[5] = (UCHAR)(OperatingCurrent >> 8) & 0xff;
		cBuf[6] = checksum(cBuf);
		cBuf[7] = 0x03;
		com->sendData((char*)cBuf, 8);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set sink capability
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x06;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_SINK_CAP;
		cBuf[4] = (UCHAR)SinkCapMV & 0xff;
		cBuf[5] = (UCHAR)(SinkCapMV >> 8) & 0xff;
		cBuf[6] = (UCHAR)SinkCapMA & 0xff;
		cBuf[7] = (UCHAR)(SinkCapMA >> 8) & 0xff;
		cBuf[8] = checksum(cBuf);
		cBuf[9] = 0x03;
		com->sendData((char*)cBuf, 10);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set config profile limits
	for (int i = 0; i < 4; i++)
	{
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG_VOLATILE;
			cBuf[3] = CFG_PROFILE_LIMIT + i;

			UCHAR limit = 0;
			if (i == 0)
				limit = ProfilePDLimit;
			else if (i == 1)
				limit = ProfileEPRLimit;
			else if (i == 2)
				limit = ProfileBCLimit;
			else if (i == 3)
				limit = ProfileQCLimit;

			cBuf[4] = limit;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}
	}

	// Set should current fail or find closest lower voltage on default config fail
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DEF_FAIL_VOLT;
		cBuf[4] = bDefaultConfigFailOnVolt;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}

	// Set should current fail or find closest lower voltage on default config fail
	if (retVal == TRUE)
	{
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG_VOLATILE;
		cBuf[3] = CFG_DEF_FAIL_CURR;
		cBuf[4] = bDefaultConfigFailOnCurr;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG_VOLATILE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;
	}


	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::GetConnectionStatus(USB_ConnectionStatus_t *port_status, BYTE* profile_index, PROFILE_TypeDef *profile, BYTE *profile_subtype, UINT16 *voltage, UINT16 *max_current, USB_ConnectionStatus_t* src_port_status, BYTE* src_profile_index, PROFILE_TypeDef* src_profile, BYTE* src_profile_subtype, UINT16* src_voltage, UINT16* src_max_current, UINT16* src_req_current, BYTE* src_type_index)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);
	
	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		*port_status = USB_STATUS_NOT_CONNECTED;
		*src_port_status = USB_STATUS_NOT_CONNECTED;
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x01;
	cBuf[2] = GET_CONSTAT;
	cBuf[3] = checksum(cBuf);
	cBuf[4] = 0x03;
	com->sendData((char*)cBuf, 5);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONSTAT)
		{
			*port_status = (USB_ConnectionStatus_t)RcvData[1];
			*profile_index = (USB_ConnectionStatus_t)RcvData[2];
			*profile = (PROFILE_TypeDef)RcvData[3];
			*profile_subtype = RcvData[4];
			*voltage = ((WORD)RcvData[6] << 8) | RcvData[5];
			*max_current = ((WORD)RcvData[8] << 8) | RcvData[7];
			*src_port_status = (USB_ConnectionStatus_t)RcvData[9];
			*src_profile_index = (USB_ConnectionStatus_t)RcvData[10];
			*src_profile = (PROFILE_TypeDef)RcvData[11];
			*src_profile_subtype = RcvData[12];
			*src_voltage = ((WORD)RcvData[14] << 8) | RcvData[13];
			*src_max_current = ((WORD)RcvData[16] << 8) | RcvData[15];
			*src_req_current = ((WORD)RcvData[18] << 8) | RcvData[17];
			*src_type_index = RcvData[19];
		}
		else
			retVal = FALSE;
	}
	else
		retVal = FALSE;
	
	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::GetCapabilities(USBPD_Capabilities_TypeDef *SourceCapabilities)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;
	BYTE	len;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x01;
	cBuf[2] = GET_PORT_CAPABILITIES;
	cBuf[3] = checksum(cBuf);
	cBuf[4] = 0x03;
	com->sendData((char*)cBuf, 5);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0 && RcvData[0] == GET_PORT_CAPABILITIES)
		{
			memset((UCHAR*)&(SourceCapabilities->Object), 0, (sizeof(USBPD_Object_TypeDef) * MAX_PROFILES));
			SourceCapabilities->NumObjects = RcvData[1];
			len = (SourceCapabilities->NumObjects * sizeof(USBPD_Object_TypeDef));
			memcpy((UCHAR*)&(SourceCapabilities->Object), &RcvData[3], len);
		}
		else
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::GetStatistics(UCHAR *temp, UINT16 *voltage, UINT16 *voltage_src, UINT16 *set_current, UINT16 *current, UINT16 *current_src)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x01;
	cBuf[2] = GET_STAT;
	cBuf[3] = checksum(cBuf);
	cBuf[4] = 0x03;
	com->sendData((char*)cBuf, 5);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0 && RcvData[0] == GET_STAT)
		{
			*temp = RcvData[1];
			*voltage = ((WORD)RcvData[3] << 8) | RcvData[2];
			*voltage_src = ((WORD)RcvData[5] << 8) | RcvData[4];
			*set_current = ((WORD)RcvData[7] << 8) | RcvData[6];
			*current = ((WORD)RcvData[9] << 8) | RcvData[8];
			*current_src = ((WORD)RcvData[11] << 8) | RcvData[10];
		}
		else
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::SetVoltage(UCHAR index, UINT16 voltage)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x04;
	cBuf[2] = SET_PORT_VOLTAGE;
	cBuf[3] = index;
	cBuf[4] = voltage & 0xff;
	cBuf[5] = (voltage >> 8) & 0xff;
	cBuf[6] = checksum(cBuf);
	cBuf[7] = 0x03;
	com->sendData((char*)cBuf, 8);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status != WAIT_OBJECT_0 || RcvData[0] != SET_PORT_VOLTAGE)
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}

BOOL PDTester::SetLoad(UINT16 set_current, UINT16 load_speed_ms)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x05;
	cBuf[2] = SET_CURRENT;
	cBuf[3] = set_current & 0xff;
	cBuf[4] = (set_current >> 8) & 0xff;
	cBuf[5] = load_speed_ms & 0xff;
	cBuf[6] = (load_speed_ms >> 8) & 0xff;
	cBuf[7] = checksum(cBuf);
	cBuf[8] = 0x03;
	com->sendData((char*)cBuf, 9);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CURRENT)
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);

	return retVal;
}


BOOL PDTester::GetCalibrationData(CalibrationChannel_t channel, BOOL* isCalibrated, int* year, int* month, int* applied1, int* measured1, int* applied2, int* measured2)
{
	UCHAR	cBuf[10];
	long status;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = GET_CALIB_DATA;
	cBuf[3] = channel;
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == GET_CALIB_DATA)
			{
				*isCalibrated = RcvData[1];
				*year = RcvData[2] + 2000;
				*month = RcvData[3];
				*applied1 = (INT16)((RcvData[5] << 8) | RcvData[4]);
				*measured1 = (INT16)((RcvData[7] << 8) | RcvData[6]);
				*applied2 = (INT16)((RcvData[9] << 8) | RcvData[8]);
				*measured2 = (INT16)((RcvData[11] << 8) | RcvData[10]);
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}

BOOL PDTester::SetCalibrationData(CalibrationChannel_t channel, int year, int month, int applied1, int measured1, int applied2, int measured2)
{
	UCHAR	cBuf[32];
	long status;
	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 12;
	cBuf[2] = SET_CALIB_DATA;
	cBuf[3] = channel;
	cBuf[4] = year - 2000;
	cBuf[5] = month;
	cBuf[6] = (UINT16)applied1 & 0xff;
	cBuf[7] = ((UINT16)applied1 >> 8) & 0xff;
	cBuf[8] = (UINT16)measured1 & 0xff;
	cBuf[9] = ((UINT16)measured1 >> 8) & 0xff;
	cBuf[10] = (UINT16)applied2 & 0xff;
	cBuf[11] = ((UINT16)applied2 >> 8) & 0xff;
	cBuf[12] = (UINT16)measured2 & 0xff;
	cBuf[13] = ((UINT16)measured2 >> 8) & 0xff;
	cBuf[14] = checksum(cBuf);
	cBuf[15] = 0x03;
	com->sendData((char*)cBuf, 16);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SET_CALIB_DATA)
			{
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}

BOOL PDTester::ResetCalibrationData(CalibrationChannel_t channel)
{
	UCHAR	cBuf[32];
	long status;
	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = RESET_CALIB_DATA;
	cBuf[3] = channel;
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == RESET_CALIB_DATA)
			{
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}

BOOL PDTester::SetUsbConnection(UINT8 port, bool isConnected)
{
	UCHAR cBuf[10];
	long status;
	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x03;
	cBuf[2] = SET_USB_CONNECTION;
	cBuf[3] = port;
	if (isConnected)
		cBuf[4] = 1;
	else
		cBuf[4] = 0;
	cBuf[5] = checksum(cBuf);
	cBuf[6] = 0x03;
	com->sendData((char*)cBuf, 7);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SET_USB_CONNECTION)
			{
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}

BOOL PDTester::StartPDAnalyzer(void (*callback)(UINT8* msgRaw))
{
	PDAnalyzerCallback = callback;
	// Increase time out to 3 second since packet bursts can cause delays
	TIME_OUT_MS = 3000;
	// Add turning on analyzer in firmware cmd here
	UCHAR	cBuf[10];
	long status;
	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = SET_PD_ANALYZER;
	cBuf[3] = 0x01; // Enable PD Anlayzer
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SET_PD_ANALYZER)
			{
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}

BOOL PDTester::StopPDAnalyzer(void)
{
	// Add turning off analyzer in firmware cmd here
	UCHAR cBuf[10];
	long status;
	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	PDAnalyzerCallback = NULL;

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = SET_PD_ANALYZER;
	cBuf[3] = 0x00; // Disable PD Anlayzer
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SET_PD_ANALYZER)
			{
				TIME_OUT_MS = 500;
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	TIME_OUT_MS = 500;
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}


PDMsg PDTester::ParsePDData(UINT8* pd_data)
{
	PDMsg msg;
	UINT16 msgHeader = (pd_data[1] << 8) | pd_data[0];
	msg.MessageType = msgHeader & 0x1F;				// 0 - 4 bits are message type
	msg.DataRole = (msgHeader & (0x01 << 5)) >> 5;		// 5th bit
	msg.SpecRev = (msgHeader & (0x03 << 6)) >> 6;	// 6 - 7 bits
	msg.PowerRole = (msgHeader & (0x01 << 8)) >> 8;		// 8th bit
	msg.MessageID = (msgHeader & (0x07 << 9)) >> 9;			// 9 - 11 bits 
	msg.NumDataObj = (msgHeader & (0x07 << 12)) >> 12;		// 12 - 14 bits
	msg.Extended = (msgHeader >> 15) & 0x01;		// 15th bit
	
	// Specification doesn't allow over 8 data objects. If we read greater
	// then set to 0 to avoid issues with reading data.
	if (msg.NumDataObj > 8)
		msg.NumDataObj = 0;


	memcpy(msg.Data, &pd_data[2], (long)msg.NumDataObj * 4);	// Each data object is 4 bytes

	return msg;
}

BOOL PDTester::SelectSrcType(UINT8 SrcType)
{
	UCHAR	cBuf[100];
	DWORD	status;
	BOOL	retVal = FALSE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = SELECT_SRC_TYPE;
	cBuf[3] = SrcType;
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SELECT_SRC_TYPE)
			{
				if (RcvData[1] == 0)
					retVal = TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return retVal;
}

BOOL PDTester::GetSrcTypes(UINT8 *SrcType, UINT8 *SrcType1Name, UINT8 *SrcType2Name, UINT8 *SrcType3Name, UINT8 *SrcType4Name, UINT8 *SrcType5Name)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x01;
	cBuf[2] = GET_SRC_TYPES;
	cBuf[3] = checksum(cBuf);
	cBuf[4] = 0x03;
	com->sendData((char*)cBuf, 5);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0 && RcvData[0] == GET_SRC_TYPES)
		{
			*SrcType = RcvData[1];
			memcpy(SrcType1Name, &RcvData[2], 16);
			memcpy(SrcType2Name, &RcvData[18], 16);
			memcpy(SrcType3Name, &RcvData[34], 16);
			memcpy(SrcType4Name, &RcvData[50], 16);
			memcpy(SrcType5Name, &RcvData[66], 16);
		}
		else
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);
	return retVal;
}

BOOL PDTester::SetSrcConfig(UINT8 SrcType, UINT8* SrcTypeName, UINT8* USBCSubType, UINT8* BCSubType, UINT8* AppleSubType,
							UINT8* PDNumProfiles, UINT8* PDSubType, UINT16* PDMinmV, UINT16* PDMaxmV, UINT16* PDMaxmA)
{
	UCHAR	cBuf[100];
	DWORD	status;
	BOOL	retVal = FALSE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x47;
	cBuf[2] = SET_SRC_CONFIG;
	cBuf[3] = SrcType;
	memcpy(&cBuf[4], SrcTypeName, 16);
	cBuf[20] = *USBCSubType;
	cBuf[21] = *BCSubType;
	cBuf[22] = *AppleSubType;
	cBuf[23] = *PDNumProfiles;
	cBuf[24] = PDSubType[0];
	cBuf[25] = (UINT16)PDMinmV[0] & 0xff;
	cBuf[26] = ((UINT16)PDMinmV[0] >> 8) & 0xff;
	cBuf[27] = (UINT16)PDMaxmV[0] & 0xff;
	cBuf[28] = ((UINT16)PDMaxmV[0] >> 8) & 0xff;
	cBuf[29] = (UINT16)PDMaxmA[0] & 0xff;
	cBuf[30] = ((UINT16)PDMaxmA[0] >> 8) & 0xff;
	cBuf[31] = PDSubType[1];
	cBuf[32] = (UINT16)PDMinmV[1] & 0xff;
	cBuf[33] = ((UINT16)PDMinmV[1] >> 8) & 0xff;
	cBuf[34] = (UINT16)PDMaxmV[1] & 0xff;
	cBuf[35] = ((UINT16)PDMaxmV[1] >> 8) & 0xff;
	cBuf[36] = (UINT16)PDMaxmA[1] & 0xff;
	cBuf[37] = ((UINT16)PDMaxmA[1] >> 8) & 0xff;
	cBuf[38] = PDSubType[2];
	cBuf[39] = (UINT16)PDMinmV[2] & 0xff;
	cBuf[40] = ((UINT16)PDMinmV[2] >> 8) & 0xff;
	cBuf[41] = (UINT16)PDMaxmV[2] & 0xff;
	cBuf[42] = ((UINT16)PDMaxmV[2] >> 8) & 0xff;
	cBuf[43] = (UINT16)PDMaxmA[2] & 0xff;
	cBuf[44] = ((UINT16)PDMaxmA[2] >> 8) & 0xff;
	cBuf[45] = PDSubType[3];
	cBuf[46] = (UINT16)PDMinmV[3] & 0xff;
	cBuf[47] = ((UINT16)PDMinmV[3] >> 8) & 0xff;
	cBuf[48] = (UINT16)PDMaxmV[3] & 0xff;
	cBuf[49] = ((UINT16)PDMaxmV[3] >> 8) & 0xff;
	cBuf[50] = (UINT16)PDMaxmA[3] & 0xff;
	cBuf[51] = ((UINT16)PDMaxmA[3] >> 8) & 0xff;
	cBuf[52] = PDSubType[4];
	cBuf[53] = (UINT16)PDMinmV[4] & 0xff;
	cBuf[54] = ((UINT16)PDMinmV[4] >> 8) & 0xff;
	cBuf[55] = (UINT16)PDMaxmV[4] & 0xff;
	cBuf[56] = ((UINT16)PDMaxmV[4] >> 8) & 0xff;
	cBuf[57] = (UINT16)PDMaxmA[4] & 0xff;
	cBuf[58] = ((UINT16)PDMaxmA[4] >> 8) & 0xff;
	cBuf[59] = PDSubType[5];
	cBuf[60] = (UINT16)PDMinmV[5] & 0xff;
	cBuf[61] = ((UINT16)PDMinmV[5] >> 8) & 0xff;
	cBuf[62] = (UINT16)PDMaxmV[5] & 0xff;
	cBuf[63] = ((UINT16)PDMaxmV[5] >> 8) & 0xff;
	cBuf[64] = (UINT16)PDMaxmA[5] & 0xff;
	cBuf[65] = ((UINT16)PDMaxmA[5] >> 8) & 0xff;
	cBuf[66] = PDSubType[6];
	cBuf[67] = (UINT16)PDMinmV[6] & 0xff;
	cBuf[68] = ((UINT16)PDMinmV[6] >> 8) & 0xff;
	cBuf[69] = (UINT16)PDMaxmV[6] & 0xff;
	cBuf[70] = ((UINT16)PDMaxmV[6] >> 8) & 0xff;
	cBuf[71] = (UINT16)PDMaxmA[6] & 0xff;
	cBuf[72] = ((UINT16)PDMaxmA[6] >> 8) & 0xff;
	cBuf[73] = checksum(cBuf);
	cBuf[74] = 0x03;
	com->sendData((char*)cBuf, 75);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SET_SRC_CONFIG)
			{
				if (RcvData[1] == 0)
					retVal = TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return retVal;
}

BOOL PDTester::GetSrcConfig(UINT8 SrcType, UINT8* SrcTypeName, UINT8* USBCSubType, UINT8* BCSubType, UINT8* AppleSubType,
							UINT8* PDNumProfiles, UINT8* PDSubType, UINT16* PDMinmV, UINT16* PDMaxmV, UINT16* PDMaxmA)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = GET_SRC_CONFIG;
	cBuf[3] = SrcType;
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0 && RcvData[0] == GET_SRC_CONFIG)
		{
			memcpy(SrcTypeName, &RcvData[1], 16);
			*USBCSubType = RcvData[17];
			*BCSubType = RcvData[18];
			*AppleSubType = RcvData[19];
			*PDNumProfiles = RcvData[20];

			if (*PDNumProfiles > 0)
			{
				PDSubType[0] = RcvData[21];
				PDMinmV[0] = ((WORD)RcvData[23] << 8) | RcvData[22];
				PDMaxmV[0] = ((WORD)RcvData[25] << 8) | RcvData[24];
				PDMaxmA[0] = ((WORD)RcvData[27] << 8) | RcvData[26];

				PDSubType[1] = RcvData[28];
				PDMinmV[1] = ((WORD)RcvData[30] << 8) | RcvData[29];
				PDMaxmV[1] = ((WORD)RcvData[32] << 8) | RcvData[31];
				PDMaxmA[1] = ((WORD)RcvData[34] << 8) | RcvData[33];

				PDSubType[2] = RcvData[35];
				PDMinmV[2] = ((WORD)RcvData[37] << 8) | RcvData[36];
				PDMaxmV[2] = ((WORD)RcvData[39] << 8) | RcvData[38];
				PDMaxmA[2] = ((WORD)RcvData[41] << 8) | RcvData[40];

				PDSubType[3] = RcvData[42];
				PDMinmV[3] = ((WORD)RcvData[44] << 8) | RcvData[43];
				PDMaxmV[3] = ((WORD)RcvData[46] << 8) | RcvData[45];
				PDMaxmA[3] = ((WORD)RcvData[48] << 8) | RcvData[47];

				PDSubType[4] = RcvData[49];
				PDMinmV[4] = ((WORD)RcvData[51] << 8) | RcvData[50];
				PDMaxmV[4] = ((WORD)RcvData[53] << 8) | RcvData[52];
				PDMaxmA[4] = ((WORD)RcvData[55] << 8) | RcvData[54];

				PDSubType[5] = RcvData[56];
				PDMinmV[5] = ((WORD)RcvData[58] << 8) | RcvData[57];
				PDMaxmV[5] = ((WORD)RcvData[60] << 8) | RcvData[59];
				PDMaxmA[5] = ((WORD)RcvData[62] << 8) | RcvData[61];

				PDSubType[6] = RcvData[63];
				PDMinmV[6] = ((WORD)RcvData[65] << 8) | RcvData[64];
				PDMaxmV[6] = ((WORD)RcvData[67] << 8) | RcvData[66];
				PDMaxmA[6] = ((WORD)RcvData[69] << 8) | RcvData[68];
			}
			else
			{
				PDSubType[0] = 0;
				PDMinmV[0] = 0;
				PDMaxmV[0] = 0;
				PDMaxmA[0] = 0;

				PDSubType[1] = 0;
				PDMinmV[1] = 0;
				PDMaxmV[1] = 0;
				PDMaxmA[1] = 0;

				PDSubType[2] = 0;
				PDMinmV[2] = 0;
				PDMaxmV[2] = 0;
				PDMaxmA[2] = 0;

				PDSubType[3] = 0;
				PDMinmV[3] = 0;
				PDMaxmV[3] = 0;
				PDMaxmA[3] = 0;

				PDSubType[4] = 0;
				PDMinmV[4] = 0;
				PDMaxmV[4] = 0;
				PDMaxmA[4] = 0;

				PDSubType[5] = 0;
				PDMinmV[5] = 0;
				PDMaxmV[5] = 0;
				PDMaxmA[5] = 0;

				PDSubType[6] = 0;
				PDMinmV[6] = 0;
				PDMaxmV[6] = 0;
				PDMaxmA[6] = 0;
			}

		}
		else
			retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);
	return retVal;
}

BOOL PDTester::SetSrcOverrideMV(UINT16 mV)
{
	UCHAR cBuf[10];
	long status;
	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x03;
	cBuf[2] = SET_SRC_OVERRIDE_MV;
	cBuf[3] = mV & 0x0000FFFF;
	cBuf[4] = (mV >> 8) & 0x0000FFFF;
	cBuf[5] = checksum(cBuf);
	cBuf[6] = 0x03;
	com->sendData((char*)cBuf, 7);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			if (RcvData[0] == SET_SRC_OVERRIDE_MV)
			{
				LeaveCriticalSection(&DeviceCritSection);
				return TRUE;
			}
		}
	}
	LeaveCriticalSection(&DeviceCritSection);
	return FALSE;
}


BOOL PDTester::RunFFT(bool enable)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x02;
	cBuf[2] = RUN_FFT;
	cBuf[3] = (enable ? 0x01 : 0x00);
	cBuf[4] = checksum(cBuf);
	cBuf[5] = 0x03;
	com->sendData((char*)cBuf, 6);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == RUN_FFT)
			{
				if (RcvData[1] == 0)
					retVal = TRUE;
			}
			else
				retVal = FALSE;
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);
	return retVal;
}

BOOL PDTester::GetFFT(INT16* FFT_Amplitudes)
{
	UCHAR	cBuf[10];
	DWORD	status;
	BOOL	retVal = TRUE;
	UINT16  i = 0;
	UINT16  j = 0;

	EnterCriticalSection(&DeviceCritSection);

	// Returning TRUE since this this is the expected reaction
	if (bConnected == FALSE) {
		LeaveCriticalSection(&DeviceCritSection);
		return TRUE;
	}

	cBuf[0] = 0x02;
	cBuf[1] = 0x01;
	cBuf[2] = GET_FFT;
	cBuf[3] = checksum(cBuf);
	cBuf[4] = 0x03;
	com->sendData((char*)cBuf, 5);
	status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
	if (status == WAIT_OBJECT_0)
	{
		for (i = 0; i < 30; i++)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0 && RcvData[0] == GET_FFT)
			{
				FFT_Amplitudes[j + 0] = (INT16)((RcvData[2] << 8) | RcvData[1]);
				FFT_Amplitudes[j + 1] = (INT16)((RcvData[4] << 8) | RcvData[3]);
				FFT_Amplitudes[j + 2] = (INT16)((RcvData[6] << 8) | RcvData[5]);
				FFT_Amplitudes[j + 3] = (INT16)((RcvData[8] << 8) | RcvData[7]);
				FFT_Amplitudes[j + 4] = (INT16)((RcvData[10] << 8) | RcvData[9]);
				FFT_Amplitudes[j + 5] = (INT16)((RcvData[12] << 8) | RcvData[11]);
				FFT_Amplitudes[j + 6] = (INT16)((RcvData[14] << 8) | RcvData[13]);
				FFT_Amplitudes[j + 7] = (INT16)((RcvData[16] << 8) | RcvData[15]);
				FFT_Amplitudes[j + 8] = (INT16)((RcvData[18] << 8) | RcvData[17]);
				FFT_Amplitudes[j + 9] = (INT16)((RcvData[20] << 8) | RcvData[19]);
				FFT_Amplitudes[j + 10] = (INT16)((RcvData[22] << 8) | RcvData[21]);
				FFT_Amplitudes[j + 11] = (INT16)((RcvData[24] << 8) | RcvData[23]);
				FFT_Amplitudes[j + 12] = (INT16)((RcvData[26] << 8) | RcvData[25]);
				FFT_Amplitudes[j + 13] = (INT16)((RcvData[28] << 8) | RcvData[27]);
				FFT_Amplitudes[j + 14] = (INT16)((RcvData[30] << 8) | RcvData[29]);
				FFT_Amplitudes[j + 15] = (INT16)((RcvData[32] << 8) | RcvData[31]);
				FFT_Amplitudes[j + 16] = (INT16)((RcvData[34] << 8) | RcvData[33]);
				FFT_Amplitudes[j + 17] = (INT16)((RcvData[36] << 8) | RcvData[35]);
				FFT_Amplitudes[j + 18] = (INT16)((RcvData[38] << 8) | RcvData[37]);
				j += 19;
			}
			else
			{
				retVal = FALSE;
				break;
			}
		}
	}
	else
		retVal = FALSE;

	LeaveCriticalSection(&DeviceCritSection);
	return retVal;
}


