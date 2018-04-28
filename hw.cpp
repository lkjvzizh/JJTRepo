 #include "windows.h"
 #include "stdio.h"
#include <string>
// #include "afx.h"

#define XON 0x11
#define XOFF 0x13
#define MAXBLOCK 2048
OVERLAPPED m_osRead, m_osWrite;		// �����ص���/д
HANDLE hCom;				// ���пھ��
HANDLE m_pThread;				// �������߳�
HANDLE m_hPostMsgEvent;	// ����WM_COMMNOTIFY��Ϣ���¼�����ʱ�䴦�����ź�״̬ʱ�ſ��Է�����Ϣ
BOOL m_bConnected;			// ��־�ʹ��ڵ�����״̬
HWND m_hTermWnd;			// ����WM_COMMNOTIFY��Ϣ����ͼ����

// ���ܣ��򿪴���
// ���룺�˿ں�
// �����FALSE:ʧ�� TRUE:�ɹ�
BOOL OpenConnection(int port)
{
	COMMTIMEOUTS TimeOuts;
	char szMsg[255];
	sprintf( szMsg, "\\\\.\\COM%d", port );
	hCom = CreateFile(szMsg, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,NULL); // �ص���ʽ
	if(hCom == INVALID_HANDLE_VALUE)
		return FALSE;

		// ���������豸����������д��������С��ΪMAXBLOK
	SetupComm(hCom, MAXBLOCK, MAXBLOCK);

	// ���ü����¼�EV_RXCHAR����ʱ���ʾ���ܵ��κ��ַ�������ܻ�������
	SetCommMask(hCom, EV_RXCHAR);

    // �Ѽ����ʱ��Ϊ��󣬰��ܳ�ʱ��Ϊ0������ReadFile�������ز���ɲ���
	TimeOuts.ReadIntervalTimeout=MAXDWORD; 
	TimeOuts.ReadTotalTimeoutMultiplier=0; 
	TimeOuts.ReadTotalTimeoutConstant=0; 
    /* ����д��ʱ��ָ��WriteComm��Ա�����е�GetOverlappedResult�����ĵȴ�ʱ��*/
	TimeOuts.WriteTotalTimeoutMultiplier=50;
	TimeOuts.WriteTotalTimeoutConstant=2000;
    SetCommTimeouts(hCom, &TimeOuts);


	if((m_hPostMsgEvent=CreateEvent(NULL, TRUE, TRUE, NULL)) == NULL)
		return FALSE;
	
	// ��ʼ�������ص���/д��OVERLAPPED�ṹ
	memset(&m_osRead, 0, sizeof(OVERLAPPED));
	memset(&m_osWrite, 0, sizeof(OVERLAPPED));
	
	// Ϊ�ص��������¼������ֹ����ã���ʼ��Ϊ���źŵ�
	if((m_osRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
		return FALSE;
	
	// Ϊ�ص�д�����¼������ֹ����ã���ʼ��Ϊ���źŵ�
	if((m_osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
		return FALSE;

	return TRUE;
}

// ���ܣ����ô����豸����
// ���룺������
// �����FALSE:ʧ�� TRUE:�ɹ�
BOOL ConfigConnection(int iBaud)
{
	DCB dcb;
	int m_nFlowCtrl=0;
	if(!GetCommState(hCom, &dcb))
		return FALSE;
	
	dcb.fBinary = TRUE;
	dcb.BaudRate = iBaud;		// ������
	dcb.ByteSize = 8;	// ÿ�ֽ�λ��
	dcb.fParity = TRUE;
	dcb.Parity = NOPARITY;		// ��У��
	dcb.StopBits = ONESTOPBIT;	// ֹͣλ
	
	// Ӳ������������
	dcb.fOutxCtsFlow = m_nFlowCtrl==1;
	dcb.fRtsControl = m_nFlowCtrl==1?
		RTS_CONTROL_HANDSHAKE:RTS_CONTROL_ENABLE;
	
	// XON/XOFF����������
	dcb.fInX = dcb.fOutX = m_nFlowCtrl == 2;
	dcb.XonChar = XON;
	dcb.XoffChar = XOFF;
	dcb.XonLim = 50;
	dcb.XoffLim = 50;
	return SetCommState(hCom, &dcb);
}

void SetReceiveHWnd(HWND hwnd)
{
	m_hTermWnd=hwnd;
}

DWORD WINAPI CommProc(LPVOID pParam)
{
	OVERLAPPED os;
	DWORD dwMask, dwTrans;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	memset(&os, 0, sizeof(OVERLAPPED));
	os.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(os.hEvent == NULL){
	/*	MessageBox("Can't create event object!");*/
		return (UINT)-1;
	}
	while(m_bConnected){
		// ����ǰͨѶ�豸״̬
		ClearCommError(hCom,&dwErrorFlags,&ComStat);
		if(ComStat.cbInQue){					// �н����ַ�
			// ���޵ȴ�WM_COMMNOTIFY��Ϣ��������
			WaitForSingleObject(m_hPostMsgEvent, INFINITE);
			ResetEvent(m_hPostMsgEvent);
			// ֪ͨ��ͼ
			PostMessage(m_hTermWnd, WM_COMMNOTIFY, EV_RXCHAR, 0);
			continue;
		}
		dwMask = 0;
		if(!WaitCommEvent(hCom, &dwMask, &os)){ // �ص�����
			if(GetLastError() == ERROR_IO_PENDING)
				// ���޵ȴ��ص�������� 
				GetOverlappedResult(hCom, &os, &dwTrans, TRUE);
			else{
				CloseHandle(os.hEvent);
				return (UINT)-1;
				}
		}
	}
	CloseHandle(os.hEvent);
	return 0;
}

BOOL StartThread()
{
	m_pThread = CreateThread(NULL,0,CommProc,NULL,0,NULL);// �����������߳�
	if (m_pThread==NULL)
	{
		CloseHandle(hCom);
		return FALSE;
	}
	else
	{
		m_bConnected=TRUE;
		ResumeThread(m_pThread);
	}
	return TRUE;
}



// ���ܣ�����������
// ���룺buf:���ܻ�����ָ�� dwLenth:����������
// ��������������ݳ���
DWORD ReadComm(char *buf,DWORD dwLength)
{
	DWORD length = 0;
	COMSTAT ComStat;	
	DWORD dwErrorFlags;
	
	// ��������־���豸��ǰ״̬��Ϣ
	ClearCommError(hCom, &dwErrorFlags, &ComStat);
	
	// ���ָ��Ҫ�����ַ������ڽ��ջ�������ʵ���ַ�����ȡ��Сֵ
	length=min(dwLength, ComStat.cbInQue);
	
	ReadFile(hCom, buf, length, &length, &m_osRead);

	PurgeComm(hCom,PURGE_TXCLEAR);//������ͻ�����
    PurgeComm(hCom,PURGE_RXCLEAR);//������ܻ�����

	return length;
}

// ���ܣ��򴮿�д������
// ���룺buf:����ָ�� dwLength:д�볤��
// ������ɹ�:д������ݳ���  ʧ��:�������
DWORD WriteComm(char *buf,DWORD dwLength)
{
	BOOL fState;
	DWORD length = dwLength;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	// ��������־���豸��ǰ״̬��Ϣ
	ClearCommError(hCom, &dwErrorFlags, &ComStat);
	fState=WriteFile(hCom, buf, length, &length, &m_osWrite);
	if(!fState){
		// ���д������δ��ɣ������ȴ�
		if(GetLastError() == ERROR_IO_PENDING)
			GetOverlappedResult(hCom, &m_osWrite, &length, TRUE);// �ȴ�
		else
			length = 0;
	}
	return length;
}


//�ر�����
BOOL CloseConnection()
{
	// ����Ѿ����Ҷϣ�����
	if(!m_bConnected) 
		return FALSE;
	m_bConnected = FALSE;
	
	//����CommProc�߳���WaitSingleObject�����ĵȴ�
	SetEvent(m_hPostMsgEvent); 
	
	//����CommProc�߳���WaitCommEvent�ĵȴ�
	SetCommMask(hCom, 0); 
	
	//�ȴ������߳���ֹ
	WaitForSingleObject(m_pThread, INFINITE);
	m_pThread = NULL;
	
	// ɾ���¼����
	if(m_hPostMsgEvent)
		CloseHandle(m_hPostMsgEvent);
	if(m_osRead.hEvent)
		CloseHandle(m_osRead.hEvent);
	if(m_osWrite.hEvent)
		CloseHandle(m_osWrite.hEvent);

	// �رմ����豸���
	return CloseHandle(hCom);
}

string ConvertCharBufToString(char *buf,int nLength)
{
	string str;
	string strReadSerialBuff;
	if (nLength)
	{
// 		for (int i=0;i<nLength;i++)
// 		{
// 			str+=buf[i];
// 		}
		str=buf;
		strReadSerialBuff=str;
		str=strReadSerialBuff.substr(strReadSerialBuff.length()-1,1);
		if (str=='$')
			return strReadSerialBuff;
	}
	return strReadSerialBuff;
}

void SerialCommandFx(string strReadSerialBuff,int nHeight,int nWeight,int nFoot,int nBmi)
{
	char ch;
	string str;
	while (strReadSerialBuff.length())
	{
		ch=strReadSerialBuff.at(0);
		if (ch=='0')
		{
			str=strReadSerialBuff.substr(1,4);
			nWeight=atoi(str);
			strReadSerialBuff.erase(0,6);
		}
		else if (ch=='1')
		{
			str=strReadSerialBuff.substr(1,4);
			nFoot=atoi(str);
			strReadSerialBuff.erase(0, 6);
		}
		else if (ch=='2')
		{
			str=strReadSerialBuff.substr(1,4);
			nHeight=atoi(str);
			strReadSerialBuff.erase(0, 6);
		}
		else if (ch=='3')//������������������ָ��
		{
			if (nHeight!=0)
			{
				double d=(float)nWeight/(nHeight*nHeight);
				nBmi=(int)(d*1000000+0.5);
			}
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='4')
		{
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='5')
		{
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='6')
		{
			strReadSerialBuff.erase(0,2);
		}
		else if (ch=='7')
		{
			strReadSerialBuff.erase(0,2);
		}
	}
}