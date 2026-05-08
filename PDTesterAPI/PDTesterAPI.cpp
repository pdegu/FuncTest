#include "PDTesterAPI.h"
	
namespace Passmark {

#define GET_DEV_INFO					0x01
#define GET_CONSTAT						0x0A
#define GET_PORT_CAPABILITIES			0x0B
#define GET_STAT						0x0C
#define SET_PORT_VOLTAGE				0x0D
#define SET_DEF_VOLTAGE					0x0E
#define SET_DEF_CURRENT					0x0F

#define	SET_CURRENT						0x10
#define	SET_CURRENT_FAST				0x11
#define SET_DEF_PROFILE 				0x12
#define GET_STEP_RESPONSE				0x13

#define SET_USB_CONNECTION				0x14
#define INJECT_PD_MSG					0x15
#define INJECT_PD_MSG_RAW				0x16
//#define SET_USB_DETTACHED				0x17
//#define SET_USB_ATTACHED				0x18

#define GET_SUB_HW_REV					0xD1

#define SET_CALIB_DATA					0xE3
#define GET_CALIB_DATA					0xE4
#define RESET_CALIB_DATA				0xE5

#define EVENT_PORT_ATTACHED				0x20
#define EVENT_PORT_DETACHED				0x21
#define EVENT_PROFILE_CHANGED			0x22
#define EVENT_NEW_CAPABILITY			0x23
#define EVENT_PD_MSG_RECV				0x24
#define EVENT_PD_MSG_SENT				0x25
#define	SET_CONFIG						0xE0
#define	GET_CONFIG						0xE1
#define SET_PD_ANALYZER					0xE7

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
#define CFG_HOLD_LOAD				0x16
#define CFG_PPS_ENABLED				0x17
#define CFG_DEF_CONF_ON_CAP			0x18
#define CFG_DEF_FAIL_VOLT			0x19
#define CFG_DEF_FAIL_CURR			0x1A

//#define	TIME_OUT_MS					500

#define MAX_MSG_INJECT_LENGTH		50

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

	BOOL PDTester::Connect(char *port, type_EventCallBack event_callback)
	{
		BOOL status = FALSE;

		InitializeCriticalSection(&DeviceCritSection);
	
		com = new Tserial_event();
		if (com != 0)
		{
			EventCallback = event_callback;
			com->setManager(SerialEventManager, this);
			com->setRxSize(SERIAL_MAX_RX);
			status = com->connect(port);
			if (status == 0)
			{
				com->setRxSize(1);
				com->dataHasBeenRead();
				return FALSE;
			}
		}

		if (!this->GetDevInfo(&HW_Ver, &FW_Ver))
			return FALSE;

		HW_SubVer = HW_Ver;
		if (HW_Ver >= 20 && HW_Ver < 30) {
			if (FW_Ver >= 46) {
				// If revision 2 and FW 4.6 or newer, then it has the sub rev command
				GetSubRev();
				HW_SubVer = HW_Ver + HW_SubRevNumber;
			}
		}

		// Need to set HW_Ver = 20 to avoid breaking things in software
		if (HW_Ver >= 20 && HW_Ver < 30) {
			HW_Ver = 20;
		}

		this->GetConfig();

		return TRUE;
	}

	void PDTester::Disconnect()
	{
		if (com != 0)
		{
			com->disconnect();
		}
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
						RcvDataLen = *ptr;
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
						memcpy(RcvData, &RcvDataTmp[0], 64);
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

	//Get the serial numbers and device number of the currently connecetd plugs
	//Return the number of plugs found or -1 for on error
	int PDTester::GetConnectedDevices(char *devices[MAX_NUM_TESTERS])
	{
		int numUSB3Plugsfound = 0;
		FT_STATUS		ftStatus;
		DWORD			devcount;

		ftStatus = FT_ListDevices(&devcount, NULL, FT_LIST_NUMBER_ONLY);

		char		serial[MAX_SERIAL_LENGTH];

		for (DWORD Index = 0; Index < devcount; Index++)
		{
			ftStatus = FT_ListDevices((PVOID)Index, serial, FT_LIST_BY_INDEX | FT_OPEN_BY_SERIAL_NUMBER);

			if (!strncmp(serial, USBPD_SERIAL_PREFIX, 4))
			{
				strcpy_s(devices[numUSB3Plugsfound], MAX_SERIAL_LENGTH, serial);
				numUSB3Plugsfound++;
			}
		}

		return  numUSB3Plugsfound;
	}

	BOOL PDTester::GetDevInfo(UCHAR *HW_Ver, UCHAR *SW_Ver)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

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
					*HW_Ver = RcvData[1];
					*SW_Ver = RcvData[2];
				}
				else
					retVal = FALSE;
			}
			else
			{
				*HW_Ver = 10;
				*SW_Ver = 0;
				retVal = FALSE;
			}
		}
		else
			retVal = FALSE;

		LeaveCriticalSection(&DeviceCritSection);

		return retVal;
	}

	BOOL PDTester::GetSubRev()
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x01;
		cBuf[2] = GET_SUB_HW_REV;
		cBuf[3] = checksum(cBuf);
		cBuf[4] = 0x03;
		com->sendData((char*)cBuf, 5);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				if (RcvData[0] == GET_SUB_HW_REV)
				{
					HW_SubRevNumber = RcvData[1];
				}
				else
					retVal = FALSE;
			}
			else
			{
				HW_SubRevNumber = 0;
				retVal = FALSE;
			}
		}
		else
			retVal = FALSE;

		LeaveCriticalSection(&DeviceCritSection);

		return retVal;
	}

	BOOL PDTester::IsLatestFirmware(void)
	{
		if (HW_Ver == 10) 
		{
			if (FW_Ver >= LATEST_FW_REV1) {
				return true;
			}
			else {
				return false;
			}
		}
		else if (HW_Ver == 20)
		{
			return (FW_Ver >= LATEST_FW_REV2);
		}
		else
		{
			return false;
		}
	}

	BOOL PDTester::GetConfig(void)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

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

		// Put these checks or else this function takes a long time doing nothing on older fimware.
		if (FW_Ver < 38) {
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

		// Put these checks or else this function takes a long time doing nothing on older fimware.
		if (FW_Ver < 41) {
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
		for (int i = 0; i < 9; i++)
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
						UINT16 uint16 = ((unsigned int)RcvData[2] << 8) + RcvData[1];
						if (i == 0)
							ProfilePDLimit = uint16;
						else if (i == 1)
							ProfileUCLimit = uint16;
						else if (i == 2)
							ProfileBCLimit = uint16;
						else if (i == 3)
							ProfileQC5Limit = uint16;
						else if (i == 4)
							ProfileQC9Limit = uint16;
						else if (i == 5)
							ProfileQC12Limit = uint16;
						else if (i == 6)
							ProfileQC20Limit = uint16;
						else if (i == 7)
							ProfileAppleLimit = uint16;
						else if (i == 8)
							ProfileSamsung2ALimit = uint16;
					}
					else
						retVal = FALSE;
				}
				else
					retVal = FALSE;
			}
		}
	
		if (FW_Ver < 42) {
			retVal = FALSE;
		}

		// Get Hold Load on voltage change
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x02;
			cBuf[2] = GET_CONFIG;
			cBuf[3] = CFG_HOLD_LOAD;
			cBuf[4] = checksum(cBuf);
			cBuf[5] = 0x03;
			com->sendData((char*)cBuf, 6);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
				{
					bHoldLoadOnVChange = RcvData[1];
				}
				else
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}

		// Get PPS Enabled
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x02;
			cBuf[2] = GET_CONFIG;
			cBuf[3] = CFG_PPS_ENABLED;
			cBuf[4] = checksum(cBuf);
			cBuf[5] = 0x03;
			com->sendData((char*)cBuf, 6);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
				{
					bPPSEnabled = RcvData[1];
				}
				else
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}

		// Get Apply Default Config On Source Cap
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
				if (status == WAIT_OBJECT_0 && RcvData[0] == GET_CONFIG && RcvDataLen > 1)
				{
					bApplyDefaultConfigOnCap = RcvData[1];
				}
				else
					retVal = FALSE;
			}
			else
				retVal = FALSE;
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

	BOOL PDTester::SetConfig(void)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		// Set config loopback enable
		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CONFIG;
		cBuf[3] = CFG_LOOPBACK_ENABLE;
		cBuf[4] = bLoopbackPortEnabled;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
				cBuf[2] = SET_CONFIG;
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
				cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_ESTIMATE_VBUS;
			cBuf[4] = bEstimatePortVoltage;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_DEFAULT_PROFILE_IDX;
			cBuf[4] = DefaultProfileIndex;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
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
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}

		// Set config profile limits
		for (int i = 0; i < 9; i++)
		{
			if (retVal == TRUE)
			{
				cBuf[0] = 0x02;
				cBuf[1] = 0x04;
				cBuf[2] = SET_CONFIG;
				cBuf[3] = CFG_PROFILE_LIMIT + i;

				UINT16 limit = 0;
				if (i == 0)
					limit = ProfilePDLimit;
				else if (i == 1)
					limit = ProfileUCLimit;
				else if (i == 2)
					limit = ProfileBCLimit;
				else if (i == 3)
					limit = ProfileQC5Limit;
				else if (i == 4)
					limit = ProfileQC9Limit;
				else if (i == 5)
					limit = ProfileQC12Limit;
				else if (i == 6)
					limit = ProfileQC20Limit;
				else if (i == 7)
					limit = ProfileAppleLimit;
				else if (i == 8)
					limit = ProfileSamsung2ALimit;

				cBuf[4] = (UCHAR)limit & 0xff;
				cBuf[5] = (UCHAR)(limit >> 8) & 0xff;
				cBuf[6] = checksum(cBuf);
				cBuf[7] = 0x03;
				com->sendData((char*)cBuf, 8);
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
				if (status == WAIT_OBJECT_0)
				{
					status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
					if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
						retVal = FALSE;
				}
				else
					retVal = FALSE;
			}
		}

		// Set hold load on voltage change
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_HOLD_LOAD;
			cBuf[4] = bHoldLoadOnVChange;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}

		// Set PPS Enabled
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_PPS_ENABLED;
			cBuf[4] = bPPSEnabled;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}

		// Set Apply Default Config On Source Cap
		if (retVal == TRUE)
		{
			cBuf[0] = 0x02;
			cBuf[1] = 0x03;
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_DEF_CONF_ON_CAP;
			cBuf[4] = bApplyDefaultConfigOnCap;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_DEF_FAIL_VOLT;
			cBuf[4] = bDefaultConfigFailOnVolt;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
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
			cBuf[2] = SET_CONFIG;
			cBuf[3] = CFG_DEF_FAIL_CURR;
			cBuf[4] = bDefaultConfigFailOnCurr;
			cBuf[5] = checksum(cBuf);
			cBuf[6] = 0x03;
			com->sendData((char*)cBuf, 7);
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
				if (status != WAIT_OBJECT_0 || RcvData[0] != SET_CONFIG)
					retVal = FALSE;
			}
			else
				retVal = FALSE;
		}

		LeaveCriticalSection(&DeviceCritSection);

		return retVal;
	}

	/* Pass config variable to function and returns whether the PD Tester
	 * supports adjusting of that variable.
	 */
	BOOL PDTester::isConfigSupported(void *config_option)
	{
		// Revision 1 only supports old config variables. Return true if it is
		// one of these. List only needs to be updated when new rev1 options added
		if (HW_Ver == 10)
		{
			if (config_option == &bLoopbackPortEnabled ||
				config_option == &bEstimatePortVoltage ||
				config_option == &CurrentLimitType ||
				config_option == &MaxCurrent ||
				config_option == &CableResistance ||
				config_option == &DefaultVoltage ||
				config_option == &DefaultLoad)
			{
				return TRUE;
			}
			else
			{
				return FALSE;
			}
		}
	
		// Revision 2 supports all config variables so return true always.
		else if (HW_Ver == 20)
		{
			return TRUE;
		}
		else 
		{
			return FALSE;
		}
	}

	BOOL PDTester::GetConnectionStatus(USB_ConnectionStatus_t *port_status, BYTE* profile_index, PROFILE_TypeDef *profile, BYTE *profile_subtype, UINT16 *voltage, UINT16 *max_current, UINT32 *max_power)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);
	
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
				*max_power = ((WORD)RcvData[12] << 24) | ((WORD)RcvData[11] << 16) | ((WORD)RcvData[10] << 8) | RcvData[9];
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

		EnterCriticalSection(&DeviceCritSection);

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
				SourceCapabilities->NumObjects = RcvData[1];
				memcpy((UCHAR*)&(SourceCapabilities->Object), &RcvData[3], sizeof(USBPD_Object_TypeDef) * 7);
				// If more than 7 caps then there will be another message
				if (SourceCapabilities->NumObjects > 7)
				{
					status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
					if (status == WAIT_OBJECT_0 && RcvData[0] == GET_PORT_CAPABILITIES)
					{
						memcpy((UCHAR*)&(SourceCapabilities->Object[7]), &RcvData[3], sizeof(USBPD_Object_TypeDef) * 7);
					}
					else
						retVal = FALSE;
				}
			}
			else
				retVal = FALSE;
		}
		else
			retVal = FALSE;

		LeaveCriticalSection(&DeviceCritSection);

		// If there was any AVS profiles then we need to do a little bit of rearranging on the data
		for (int i = 0; i < MAX_PROFILES; i++) {
			if (SourceCapabilities->Object[i].Profile.Type == PROFILE_PD && SourceCapabilities->Object[i].Profile.SubType == SUBTYPE_PD_AVS) {
				SourceCapabilities->objExtraDataField[i] = SourceCapabilities->Object[i].MinVoltage;
				SourceCapabilities->Object[i].MinVoltage = 9000;
			}
		}

		return retVal;
	}

	BOOL PDTester::GetStatistics(UCHAR *temp, UINT16 *voltage, UINT16 *set_current, UINT16 *current, UINT16 *loopback_current)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

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
				*temp = RcvData[2];
				*voltage = ((WORD)RcvData[4] << 8) | RcvData[3];
				*set_current = ((WORD)RcvData[6] << 8) | RcvData[5];
				*current = ((WORD)RcvData[8] << 8) | RcvData[7];
				*loopback_current = ((WORD)RcvData[10] << 8) | RcvData[9];
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

	BOOL PDTester::SetLoad(UINT16 set_current)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_CURRENT;
		cBuf[3] = set_current & 0xff;
		cBuf[4] = (set_current >> 8) & 0xff;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
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

	BOOL PDTester::SetLoadFast(UINT16 set_current, UINT16 slope)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x05;
		//cBuf[1] = 0x03;
		cBuf[2] = SET_CURRENT_FAST;
		cBuf[3] = set_current & 0xff;
		cBuf[4] = (set_current >> 8) & 0xff;
		cBuf[5] = slope & 0xff;
		cBuf[6] = (slope >> 8) & 0xff;
		cBuf[7] = checksum(cBuf);
		cBuf[8] = 0x03;
		com->sendData((char*)cBuf, 9);
		//cBuf[5] = checksum(cBuf); // For default fast speed
		//cBuf[6] = 0x03;
		//com->sendData((char*)cBuf, 7);
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

	BOOL PDTester::SetDefaultVoltage(UINT16 voltage_mv)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_DEF_VOLTAGE;
		cBuf[3] = voltage_mv & 0xff;
		cBuf[4] = (voltage_mv >> 8) & 0xff;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_DEF_VOLTAGE)
				retVal = FALSE;
		}
		else
			retVal = FALSE;

		LeaveCriticalSection(&DeviceCritSection);

		return retVal;
	}

	BOOL PDTester::SetDefaultLoad(UINT16 current_ma)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x03;
		cBuf[2] = SET_DEF_CURRENT;
		cBuf[3] = current_ma & 0xff;
		cBuf[4] = (current_ma >> 8) & 0xff;
		cBuf[5] = checksum(cBuf);
		cBuf[6] = 0x03;
		com->sendData((char*)cBuf, 7);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status != WAIT_OBJECT_0 || RcvData[0] != SET_DEF_CURRENT)
				retVal = FALSE;
		}
		else
			retVal = FALSE;

		LeaveCriticalSection(&DeviceCritSection);

		return retVal;
	}

	BOOL PDTester::GetStepResponse(UINT16 start_current, UINT16 end_current, UINT16 *voltages, UINT8 *sample_time_us)
	{
		UCHAR	cBuf[10];
		DWORD	status;
		BOOL	retVal = TRUE;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x05;
		cBuf[2] = GET_STEP_RESPONSE;
		cBuf[3] = start_current & 0xff;
		cBuf[4] = (start_current >> 8) & 0xff;
		cBuf[5] = end_current & 0xff;
		cBuf[6] = (end_current >> 8) & 0xff;
		cBuf[7] = checksum(cBuf);
		cBuf[8] = 0x03;
		com->sendData((char*)cBuf, 9);
		const int voltages_per_message = 25;
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, 4000);
		if (status == WAIT_OBJECT_0)
		{
			for (int i = 0; i < 3 && retVal == TRUE; i++) // Voltages sent over 3 messages
			{
				status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, 6000);
				if (status == WAIT_OBJECT_0 && RcvData[0] == GET_STEP_RESPONSE)
				{
					int offset = i * voltages_per_message;
					memcpy(&voltages[offset], &RcvData[2], voltages_per_message*2);
					if (sample_time_us != NULL)
						*sample_time_us = RcvData[1];
				}
				else
					retVal = FALSE;
			}
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
					*year = RcvData[2] + 2020;
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

		cBuf[0] = 0x02;
		cBuf[1] = 12;
		cBuf[2] = SET_CALIB_DATA;
		cBuf[3] = channel;
		cBuf[4] = year - 2020;
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

	BOOL PDTester::SetUsbConnection(bool isConnected)
	{
		UCHAR	cBuf[10];
		long status;
		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = 0x02;
		cBuf[2] = SET_USB_CONNECTION;
		if (isConnected)
			cBuf[3] = 1;
		else
			cBuf[3] = 0;
		cBuf[4] = checksum(cBuf);
		cBuf[5] = 0x03;
		com->sendData((char*)cBuf, 6);
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
		UCHAR	cBuf[10];
		long status;
		EnterCriticalSection(&DeviceCritSection);

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

	// Assumes msg is of correct format
	// Also currently only up to 50 byte messages
	// NOTE: Data message with no data will not be sent by pd tester
	BOOL PDTester::InjectPDMsg(PDMsgType_t type, UINT8 *data, unsigned int dataLen)
	{
		UCHAR	cBuf[64];
		long status;

		// PDTester has issues when receiving around 32 bytes, not sure why so pad the message with 0s
		// to avoid the issue happening
		const int numPadZeros = 10;
		bool padZeros = false;
		if (dataLen > 24)
			padZeros = true;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = dataLen + 3;
		cBuf[2] = INJECT_PD_MSG;
		cBuf[3] = ((UINT8) type) & 0x1F;
		cBuf[4] = ((UINT8) type) >> 5; // Way enum is setup so after 5th bit is form
		memcpy(&cBuf[5], data, dataLen);
		if (padZeros)
		{
			UINT8 zeros[numPadZeros] = { 0 };
			memcpy(&cBuf[5 + dataLen], zeros, numPadZeros);
			dataLen += numPadZeros;
			cBuf[1] = dataLen + 3;
		}
		cBuf[5 + dataLen] = checksum(cBuf);
		cBuf[6 + dataLen] = 0x03;
		com->sendData((char*)cBuf, 7 + dataLen);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				if (RcvData[0] == INJECT_PD_MSG)
				{
					LeaveCriticalSection(&DeviceCritSection);
					if (RcvData[1] == 0)
						return TRUE;
					else
						return FALSE;
				}
			}
		}
		LeaveCriticalSection(&DeviceCritSection);
		return FALSE;

	}

	// Assumes msg is of correct format
	// Also currently only up to 50 byte messages
	BOOL PDTester::InjectPDMsgRaw(UINT8* rawMsg, unsigned int numBytes)
	{
		UCHAR	cBuf[64];
		long status;

		// PDTester has issues when receiving around 32 bytes, not sure why so pad the message with 0s
		// to avoid the issue happening
		const int numPadZeros = 10;
		bool padZeros = false;
		if (numBytes > 24)
			padZeros = true;

		// Cap number of bytes to be sent to the maximum allowed
		if (numBytes > MAX_MSG_INJECT_LENGTH)
			numBytes = MAX_MSG_INJECT_LENGTH;

		EnterCriticalSection(&DeviceCritSection);

		cBuf[0] = 0x02;
		cBuf[1] = numBytes + 1;
		cBuf[2] = INJECT_PD_MSG_RAW;
		memcpy(&cBuf[3], rawMsg, numBytes);
		if (padZeros)
		{
			UINT8 zeros[numPadZeros] = { 0 };
			memcpy(&cBuf[5 + numBytes], zeros, numPadZeros);
			numBytes += numPadZeros;
			cBuf[1] = numBytes + 3;
		}
		cBuf[3 + numBytes] = checksum(cBuf);
		cBuf[4 + numBytes] = 0x03;
		com->sendData((char*)cBuf, 5 + numBytes);
		status = WaitForMultipleObjects(1, &com_events[COM_PACKET_SENT], FALSE, TIME_OUT_MS);
		if (status == WAIT_OBJECT_0)
		{
			status = WaitForMultipleObjects(1, &com_events[COM_PACKET_ARRIVED], FALSE, TIME_OUT_MS);
			if (status == WAIT_OBJECT_0)
			{
				if (RcvData[0] == INJECT_PD_MSG_RAW)
				{
					LeaveCriticalSection(&DeviceCritSection);
					if (RcvData[1] == 0)
						return TRUE;
					else
					{
						return FALSE;

					}
				}
			}
		}
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

}

