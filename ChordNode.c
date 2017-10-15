#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>
#include <process.h>

#define FNameMax 32 /* Max length of File Name */
#define FileMax 32 /* Max number of Files */
#define baseM 6 /* base number */
#define ringSize 64 /* ringSize = 2^baseM */
#define fBufSize 1024 /* file buffer size */

typedef struct { /* Node Info Type Structure */
	int ID; /* ID */
	struct sockaddr_in addrInfo;/* Socket address */
} nodeInfoType;

typedef struct { /* File Type Structure */
	char Name[FNameMax]; /* File Name */
	int Key; /* File Key */
	nodeInfoType owner; /* Owner's Node */
	nodeInfoType refOwner; /* Ref Owner's Node */
} fileRefType;

typedef struct { /* Global Information of Current Files */
	unsigned int fileNum; /* Number of files */
	fileRefType fileRef[FileMax]; /* The Number of Current Files */
} fileInfoType;

typedef struct { /* Finger Table Structure */
	nodeInfoType Pre; /* Predecessor pointer */
	nodeInfoType finger[baseM]; /* Fingers (array of pointers) */
} fingerInfoType;

typedef struct { /* Chord Information Structure */
	fileInfoType FRefInfo; /* File Ref Own Information */
	fingerInfoType fingerInfo; /* Finger Table Information */
} chordInfoType;

typedef struct { /* Node Structure */
	nodeInfoType nodeInfo; /* Node's IPv4 Address */
	fileInfoType fileInfo; /* File Own Information */
	chordInfoType chordInfo; /* Chord Data Information */
} nodeType;

typedef struct {
	unsigned short msgID; // message ID
	unsigned short msgType; // message type (0: request, 1: response)
	nodeInfoType nodeInfo; // node address info
	short moreInfo; // more info
	fileRefType fileInfo; // file (reference) info
	unsigned int bodySize; // body size in Bytes
} chordHeaderType; // CHORD message header type

void procRecvMsg(void *);
// thread function for handling receiving messages

void procPPandFF(void *);
// thread function for sending ping messages and fixfinger

int recvn(SOCKET s, char *buf, int len, int flags);
// For receiving a file

unsigned strHash(const char *);
// A Simple Hash Function from a string to the ID/key space

int twoPow(int power);
// For getting a power of 2

int modMinus(int modN, int minuend, int subtrand);
// For modN modular operation of "minend - subtrand"

int modPlus(int modN, int addend1, int addend2);
// For modN modular operation of "addend1 + addend2"

int modIn(int modN, int targNum, int range1, int range2, int leftmode, int rightmode);
// For checking if targNum is "in" the range using left and right modes

// under modN modular environment
char *fgetsCleanup(char *);
// For handling fgets function

void flushStdin(void);
// For flushing stdin

void showCommand(void);
// For showing commands


//=====================================================================
SOCKET serverSock;
//---------------------
nodeInfoType find_successor(nodeInfoType curNode, int IDKey);
nodeInfoType find_predecessor(nodeInfoType curNode, int IDKey);
int fix_finger(nodeInfoType curNode);
// For looking up a file with filekey at a node curNode
// Found node pointer is stored at *foundNode
// May return the result
// For stabilizing the Predecessor and the successor around Node
// For targetNode's asking askNode to change its predecessor  
// For moving file keys from Node with fromNodeID to Node with toNodeID
// when joining or leaving a node
// May return the result


//=====================================================================
nodeType myNode = { 0 }; // node information -> global variable
SOCKET rqSock, rpSock, flSock, frSock, fsSock, pfSock;


HANDLE hMutex;
int sMode = 1; // silent mode


int modIn(int modN, int targNum, int range1, int range2, int leftmode, int rightmode)
// leftmode, rightmode: 0 => range boundary not included, 1 => range boundary included
{
	int result = 0;
	if (range1 == range2) {
		if ((leftmode == 0) || (rightmode == 0))
			return 0;
	}

	if (modPlus(ringSize, range1, 1) == range2) {
		if ((leftmode == 0) && (rightmode == 0))
			return 0;
	}

	if (leftmode == 0)
		range1 = modPlus(ringSize, range1, 1);
	if (rightmode == 0)
		range2 = modMinus(ringSize, range2, 1);

	if (range1 < range2) {
		if ((targNum >= range1) && (targNum <= range2))
			result = 1;
	}
	else if (range1 > range2) {
		if (((targNum >= range1) && (targNum < modN)) || ((targNum >= 0) && (targNum <= range2)))
			result = 1;
	}
	else if ((targNum == range1) && (targNum == range2))
		result = 1;
	return result;
}

int twoPow(int power)
{
	int i;
	int result = 1;

	if (power >= 0)
		for (i = 0; i<power; i++)
			result *= 2;
	else
		result = -1;

	return result;
}

int modMinus(int modN, int minuend, int subtrand)
{
	if (minuend - subtrand >= 0)
		return minuend - subtrand;
	else
		return (modN - subtrand) + minuend;
}

int modPlus(int modN, int addend1, int addend2)
{
	if (addend1 + addend2 < modN)
		return addend1 + addend2;
	else
		return (addend1 + addend2) - modN;
}

void showCommand(void)
{
	printf("CHORD> Enter a command - (c)reate: Create the chord network\n");
	printf("CHORD> Enter a command - (j)oin : Join the chord network\n");
	printf("CHORD> Enter a command - (l)eave : Leave the chord network\n");
	printf("CHORD> Enter a command - (a)dd : Add a file to the network\n");
	printf("CHORD> Enter a command - (d)elete: Delete a file to thenetwork\n");
	printf("CHORD> Enter a command - (s)earch: File search and download\n");
	printf("CHORD> Enter a command - (f)inger: Show the finger table\n");
	printf("CHORD> Enter a command - (i)nfo : Show the node information\n");
	printf("CHORD> Enter a command - (h)elp : Show the help message\n");
	printf("CHORD> Enter a command - (q)uit : Quit the program\n");
}

char *fgetsCleanup(char *string)
{
	if (string[strlen(string) - 1] == '\n')
		string[strlen(string) - 1] = '\0';
	else
		flushStdin();

	return string;
}

void flushStdin(void)
{
	int ch;

	//seek
	fseek(stdin, 0, SEEK_END);

	if (ftell(stdin) > 0)
		do
			ch = getchar();
	while (ch != EOF && ch != '\n');
}

static const unsigned char sTable[256] =
{
	0xa3,0xd7,0x09,0x83,0xf8,0x48,0xf6,0xf4,0xb3,0x21,0x15,0x78,0x99,0xb1,0xaf,0xf9,
	0xe7,0x2d,0x4d,0x8a,0xce,0x4c,0xca,0x2e,0x52,0x95,0xd9,0x1e,0x4e,0x38,0x44,0x28,
	0x0a,0xdf,0x02,0xa0,0x17,0xf1,0x60,0x68,0x12,0xb7,0x7a,0xc3,0xe9,0xfa,0x3d,0x53,
	0x96,0x84,0x6b,0xba,0xf2,0x63,0x9a,0x19,0x7c,0xae,0xe5,0xf5,0xf7,0x16,0x6a,0xa2,
	0x39,0xb6,0x7b,0x0f,0xc1,0x93,0x81,0x1b,0xee,0xb4,0x1a,0xea,0xd0,0x91,0x2f,0xb8,
	0x55,0xb9,0xda,0x85,0x3f,0x41,0xbf,0xe0,0x5a,0x58,0x80,0x5f,0x66,0x0b,0xd8,0x90,
	0x35,0xd5,0xc0,0xa7,0x33,0x06,0x65,0x69,0x45,0x00,0x94,0x56,0x6d,0x98,0x9b,0x76,
	0x97,0xfc,0xb2,0xc2,0xb0,0xfe,0xdb,0x20,0xe1,0xeb,0xd6,0xe4,0xdd,0x47,0x4a,0x1d,
	0x42,0xed,0x9e,0x6e,0x49,0x3c,0xcd,0x43,0x27,0xd2,0x07,0xd4,0xde,0xc7,0x67,0x18,
	0x89,0xcb,0x30,0x1f,0x8d,0xc6,0x8f,0xaa,0xc8,0x74,0xdc,0xc9,0x5d,0x5c,0x31,0xa4, 0x70, 0x88, 0x61, 0x2c,
	0x9f, 0x0d, 0x2b, 0x87, 0x50, 0x82, 0x54, 0x64, 0x26, 0x7d, 0x03, 0x40,
	0x34, 0x4b, 0x1c, 0x73, 0xd1, 0xc4, 0xfd, 0x3b, 0xcc, 0xfb, 0x7f, 0xab, 0xe6, 0x3e, 0x5b, 0xa5,
	0xad, 0x04, 0x23, 0x9c, 0x14, 0x51, 0x22, 0xf0, 0x29, 0x79, 0x71, 0x7e, 0xff, 0x8c, 0x0e, 0xe2,
	0x0c, 0xef, 0xbc, 0x72, 0x75, 0x6f, 0x37, 0xa1, 0xec, 0xd3, 0x8e, 0x62, 0x8b, 0x86, 0x10, 0xe8,
	0x08, 0x77, 0x11, 0xbe, 0x92, 0x4f, 0x24, 0xc5, 0x32, 0x36, 0x9d, 0xcf, 0xf3, 0xa6, 0xbb, 0xac,
	0x5e, 0x6c, 0xa9, 0x13, 0x57, 0x25, 0xb5, 0xe3, 0xbd, 0xa8, 0x3a, 0x01, 0x05, 0x59, 0x2a, 0x46
};


#define PRIME_MULT 1717

unsigned int strHash(const char *str) /* Hash: String to Key */
{
	unsigned int len = sizeof(str);
	unsigned int hash = len, i;

	for (i = 0; i != len; i++, str++)
	{
		hash ^= sTable[(*str + i) & 255];
		hash = hash * PRIME_MULT;
	}

	return hash % ringSize;
}

int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;

		left -= received;
		ptr += received;
	}
	return (len - left);
}

int main(int argc, char *argv[])
{
	WSADATA wsaData;
	HANDLE hThread[2];

	int exitFlag = 0; // indicates termination condition
	char command[7];
	char cmdChar = '\0';
	int joinFlag = 0; // indicates the join/create status
	char tempIP[16];
	char tempPort[6];
	char fileName[FNameMax + 1];
	char fileBuf[fBufSize];	
	char strSockAddr[21];

	struct sockaddr_in peerAddr, targetAddr;


	chordHeaderType tempMsg, bufMsg;

	int optVal = 5000; // 5 seconds
	int retVal; // return value

	nodeInfoType succNode, predNode, targetNode;
	fileInfoType keysInfo;
	fileRefType refInfo;
	FILE *fp;
	int i, j, targetKey, addrSize, fileSize, numTotal=0, searchResult=0 ,resultFlag=0;
	int check = 0;

	
	/* step 0: Program Initialization */
	/* step 1: Commnad line argument handling */
	/* step 2: Winsock handling */
	/* step 3: Prompt handling (loop) */
	/* step 4: User input processing (switch) */
	/* step 5: Program termination */
	/* step 0 */
	printf("*****************************************************************\n");
	printf("*          DHT-Based P2P Protocol (CHORD) Node Controller       *\n");
	printf("*                        Using by C Language                    *\n");
	printf("*                      Made by Lee Seung Yong                   *\n");
	printf("*****************************************************************\n\n");


	/* step 1: Commnad line argument handling */
	myNode.nodeInfo.addrInfo.sin_family = AF_INET;

	if (argc != 3) {
		printf("\a[ERROR] Usage : %s <IP Addr> <Port No(49152~65535)>\n", argv[0]);
		exit(1);
	}

	if ((myNode.nodeInfo.addrInfo.sin_addr.s_addr = inet_addr(argv[1])) ==
		INADDR_NONE) {
		printf("\a[ERROR] <IP Addr> is wrong!\n");
		exit(1);
	}

	if (atoi(argv[2]) > 65535 || atoi(argv[2]) < 49152) {
		printf("\a[ERROR] <Port No> should be in [49152, 65535]!\n");
		exit(1);
	}

	myNode.nodeInfo.addrInfo.sin_port = htons(atoi(argv[2]));
	strcpy(strSockAddr, argv[2]);
	strcat(strSockAddr, argv[1]);
	printf("strSoclAddr: %s\n", strSockAddr);
	myNode.nodeInfo.ID = strHash(strSockAddr);

	printf(">>> Welcome to ChordNode Program! \n");
	printf(">>> Your IP address: %s, Port No: %d, ID: %d \n", argv[1], atoi(argv[2]), myNode.nodeInfo.ID);
	printf(">>> Silent Mode is ON!\n\n");


	/* step 2: Winsock handling */
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { /* Load Winsock 2.2 DLL*/
		printf("\a[ERROR] WSAStartup() error!");
		exit(1);
	}

	hMutex = CreateMutex(NULL, FALSE, NULL);

	if (hMutex == NULL) {
		printf("\a[ERROR] CreateMutex() error!");
		exit(1);
	}

	

	/* step 3: Prompt handling (loop) */
	showCommand();


	do {
		while (1) {
			printf("CHORD> \n");
			printf("CHORD> Enter your command ('help' for help message).\n");
			printf("CHORD> ");

			fgets(command, sizeof(command), stdin);
			fgetsCleanup(command);

			if (!strcmp(command, "c") || !strcmp(command, "create"))
				cmdChar = 'c';
			else if (!strcmp(command, "j") || !strcmp(command, "join"))
				cmdChar = 'j';
			else if (!strcmp(command, "l") || !strcmp(command, "leave"))
				cmdChar = 'l';
			else if (!strcmp(command, "a") || !strcmp(command, "add"))
				cmdChar = 'a';
			else if (!strcmp(command, "d") || !strcmp(command, "delete"))
				cmdChar = 'd';
			else if (!strcmp(command, "s") || !strcmp(command, "search"))
				cmdChar = 's';
			else if (!strcmp(command, "f") || !strcmp(command, "finger"))
				cmdChar = 'f';
			else if (!strcmp(command, "i") || !strcmp(command, "info"))
				cmdChar = 'i';
			else if (!strcmp(command, "h") || !strcmp(command, "help"))
				cmdChar = 'h';
			else if (!strcmp(command, "m") || !strcmp(command, "mute"))
				cmdChar = 'm';
			else if (!strcmp(command, "q") || !strcmp(command, "quit"))
				cmdChar = 'q';
			else if (!strlen(command))
				continue;
			else {
				printf("\a[ERROR] Wrong command! Input a correctcommand.\n\n");
				continue;
			}
			break;
		}


		/* step 4: User input processing (switch) */
		switch (cmdChar) {
		case 'c':
			if (joinFlag) {
				printf("\a[ERROR] You are currently in the network; You cannot create the network!\n\n");
				continue;
			}
			joinFlag = 1;
			printf("CHORD> You have created a chord network!\n");

			// fill up the finger table information with myself
			myNode.chordInfo.fingerInfo.Pre = myNode.nodeInfo;

			for (i = 0; i<baseM; i++)
				myNode.chordInfo.fingerInfo.finger[i] = myNode.nodeInfo;

			printf("CHORD> Your finger table has been updated!\n");

			// UDP sockets creation for request and response
			rqSock = socket(AF_INET, SOCK_DGRAM, 0); // for request
			rpSock = socket(AF_INET, SOCK_DGRAM, 0); // for response

			retVal = setsockopt(rqSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));

			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] setsockopt() Error!\n");
				exit(1);
			}

			retVal = setsockopt(rpSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));

			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] setsockopt() Error!\n");
				exit(1);
			}

			if (bind(rpSock, (struct sockaddr *) &myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) < 0) {
				printf("\a[ERROR] Response port bind failed!\n");
				exit(1);
			}

			flSock = socket(AF_INET, SOCK_STREAM, 0); // for accepting file down request 

			if (bind(flSock, (SOCKADDR*)&myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) == SOCKET_ERROR) {
				printf("\a[ERROR] bind() error!\n");
				exit(1);
			}

			retVal = listen(flSock, SOMAXCONN);
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] listen() error!\n"); // for file sending
				exit(1);
			}

			// threads creation for processing incoming request message
			hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void *)procRecvMsg, (void *)&exitFlag, 0, NULL);
			// threads creation for processing sending ping message 
			hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void *)procPPandFF, (void *)&exitFlag, 0, NULL);

			break;
		case 'j':
			check = 0;
			if (joinFlag) {
				printf("\a[ERROR] You are currently in the network; You cannot join again!\n\n");
				continue;
			}
			joinFlag = 1;

			rqSock = socket(AF_INET, SOCK_DGRAM, 0);
			rpSock = socket(AF_INET, SOCK_DGRAM, 0);

			printf("CHORD> You need a helper node to join the exiting network.\n");
			printf("CHORD> If you want to create a network, the helper node is yourself.\n");
			printf("CHORD> Enter IP address of the helper node : ");
			fgets(tempIP, sizeof(tempIP), stdin);
			flushStdin();
			printf("CHORD> Enter port number of the helper node : ");
			fgets(tempPort, sizeof(tempPort), stdin);
			flushStdin();

			retVal = setsockopt(rqSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] setsockopt() Error!\n");
				exit(1);
			}
			retVal = setsockopt(rpSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));

			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] setsockopt() Error!\n");
				exit(1);
			}

			if (bind(rpSock, (struct sockaddr *) &myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) < 0) {
				printf("\a[ERROR] Response port bind failed!\n");
				exit(1);
			}

			flSock = socket(AF_INET, SOCK_STREAM, 0); // for accepting file down request 

			if (bind(flSock, (SOCKADDR*)&myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) == SOCKET_ERROR) {
				printf("\a[ERROR] bind() error!\n");
				exit(1);
			}

			retVal = listen(flSock, SOMAXCONN);
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] listen() error!\n"); // for file sending
				exit(1);
			}

			// threads creation for processing incoming request message
			hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void *)procRecvMsg, (void *)&exitFlag, 0, NULL);
			// threads creation for processing sending ping message 
			hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void *)procPPandFF, (void *)&exitFlag, 0, NULL);

			// join info
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 1;  // join info
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = 0; // 단순한 결과
			tempMsg.nodeInfo = myNode.nodeInfo;

			memset(&targetAddr, 0, sizeof(targetAddr));
			targetAddr.sin_family = AF_INET;
			targetAddr.sin_addr.s_addr = inet_addr(tempIP);
			targetAddr.sin_port = htons(atoi(tempPort));


			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &targetAddr, sizeof(targetAddr));
			
			printf("CHORD> Join info message has been sent.\n");

			retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			
			printf("CHORD> JoinInfo response Message has been received\n");
			myNode.chordInfo.fingerInfo.finger[0] = bufMsg.nodeInfo;   
			succNode = bufMsg.nodeInfo;
			fix_finger(myNode.nodeInfo);
			
			if ((bufMsg.msgID == 1) && (bufMsg.msgType == 1)) { 
				printf("CHORD> You got your successor node from the helper node.\n");
				printf("CHORD> Successor IP Addr : %s, Port No: %d , ID : %d\n", inet_ntoa(bufMsg.nodeInfo.addrInfo.sin_addr), ntohs(bufMsg.nodeInfo.addrInfo.sin_port), bufMsg.nodeInfo.ID);
			}

			// MoveKeys 도 요청
			printf("CHORD> MoveKeys request Message has been sent.\n");

			while (1) {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 2;  // Pred info
				tempMsg.msgType = 0; // request
				tempMsg.moreInfo = 0; // 단순한 결과
				tempMsg.nodeInfo = myNode.nodeInfo;

				addrSize = sizeof(succNode);
				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &succNode.addrInfo, addrSize);

				retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

				if (bufMsg.fileInfo.Key == -1) {
					break;
				}
				else {
					myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum] = bufMsg.fileInfo;
					myNode.chordInfo.FRefInfo.fileNum++;
					check++;
				}
			}
			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++) {
				printf("CHORD> %d 번째 파일 %s 의 key : %d\n", i + 1, myNode.chordInfo.FRefInfo.fileRef[i].Name, myNode.chordInfo.FRefInfo.fileRef[i].Key);
			}

			printf("CHORD> You got %d keys from your successor node\n",check);
			printf("CHORD> MoveKeys response Message has been received.\n");
			
			// Prede Info요청
			printf("CHORD> PredInfo request Message has been sent.\n");

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 3;  // Pred info
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = 0; // 단순한 결과
			addrSize = sizeof(succNode);

			printf("CHORD> Successor IP Addr : %s, Port No: %d , ID : %d\n", inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &succNode.addrInfo, addrSize);

			retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

			printf("CHORD> PredInfo response Message has been received.\n");
			predNode = bufMsg.nodeInfo;

			if ((bufMsg.msgID == 3) && (bufMsg.msgType == 1)) {
				printf("CHORD> You got your predecessor node from your successor node.\n");
				printf("CHORD> Predecessor IP Addr : %s, Port No: %d , ID : %d\n", inet_ntoa(bufMsg.nodeInfo.addrInfo.sin_addr), ntohs(bufMsg.nodeInfo.addrInfo.sin_port), bufMsg.nodeInfo.ID);
			}

			// suc UPdate 과정
			printf("CHORD> SuccUpdate request Message has been sent.\n");

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 6;  // suc Update
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = 0; // 단순한 결과
			tempMsg.nodeInfo = myNode.nodeInfo;
			addrSize = sizeof(predNode);
			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &predNode.addrInfo, addrSize);
			
			retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

			printf("CHORD> SuccUpdate response Message has been received.\n");
			printf("CHORD> Your predecessoos's successor has been updated as your node.\n");

			// Pre UPdate 과정
			printf("CHORD> PredUpdate request Message has been sent.\n");

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 4;  // Pre Update
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = 0; // 단순한 결과
			tempMsg.nodeInfo = myNode.nodeInfo;
			addrSize = sizeof(succNode);
			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &succNode.addrInfo, addrSize);

			retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

			printf("CHORD> PredUpdate response Message has been received.\n");
			printf("CHORD> Your successor's predecessoos has been updated as your node.\n");

		
			printf("CHORD> FindPred request Message has been sent.\n");

			//Find pred
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 7;  // Find pred
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = myNode.nodeInfo.ID;
			
			printf("CHORD> Receiver IP Addr : %s, Port No: %d\n", inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port));

			addrSize = sizeof(succNode);
			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &succNode.addrInfo, addrSize);

			retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
		

			printf("CHORD> FindPred response Message has been received.\n");
			myNode.chordInfo.fingerInfo.Pre = bufMsg.nodeInfo;   
			

			// suc Info
			printf("CHORD> SuccInfo request Message has been sent.\n");

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;  // suc Info
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = 0; // 단순한 결과
			
			addrSize = sizeof(predNode);
			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &predNode.addrInfo, addrSize);

			retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

			printf("CHORD> SuccInfo response Message has been received.\n");
			printf("CHORD> Node join has been successfully finished.\n");
			
			
			break;
		case 'a':    // file add

			resultFlag = 0;
			WaitForSingleObject(hMutex, INFINITE);
			if (myNode.fileInfo.fileNum == FileMax) { // file number is full 
				printf("\a[ERROR] Your Cannot Add more file. File Space is Full!\n\n");
				continue;
			}
			ReleaseMutex(hMutex);
			// input the file name
			printf("CHORD> Files to be added must be in the same folder where this program is located.\n");
			printf("CHORD> Note that the maximum file name size is %d.\n", FNameMax);
			printf("CHORD> Enter the file name to add: ");
			fgets(fileName, sizeof(fileName), stdin);
			fgetsCleanup(fileName);

			// check if the file exits
			if ((fp = fopen(fileName, "rb")) == NULL) {
				printf("\a[ERROR] The file '%s' is not in the same folder where this program is!\n\n", fileName);
				continue;
			}
			fclose(fp);

			// calculate the key value for the file
			targetKey = strHash(fileName);
			printf("CHORD> Input File Name: %s, Key: %d\n", fileName, targetKey);

			succNode = find_successor(myNode.nodeInfo,targetKey);  
			printf("CHORD> File Successor IP Addr : %s, Port No : %d, ID : %d \n", inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);
			// create file reference add request message
			refInfo.Key = targetKey;
			strcpy(refInfo.Name, fileName);
			refInfo.owner = myNode.nodeInfo;
			refInfo.refOwner = succNode;

			if (succNode.ID == myNode.nodeInfo.ID) {
				WaitForSingleObject(hMutex, INFINITE);
				myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum] = refInfo;
				myNode.chordInfo.FRefInfo.fileNum++;
				ReleaseMutex(hMutex);
			}
			else {
				peerAddr = succNode.addrInfo;
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 9;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.moreInfo = 1;
				tempMsg.fileInfo = refInfo;
				tempMsg.bodySize = 0;

				// send a file reference add request message to the target node
				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(peerAddr));

				// receive a file reference add response message from the target node
				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] FileRefAdd Request timed out. File Add Failed!\n");
						continue;
					}
					printf("\a[ERROR] FileRefAdd Recvfrom Error. File Add Failed!\n");
					continue;
				}

				if ((bufMsg.msgID != 9) || (bufMsg.msgType != 1)) { // wrong msg
					printf("\a[ERROR] Wrong Message (not FileRefAdd) Received. File Add Failed!\n");
					continue;
				}

				if (bufMsg.moreInfo == -1) { // failure
					printf("\a[ERROR] FileRefAdd Request Failed. File Add Failed!\n");
					continue;
				}
			}

			// fileInfo Update  내꺼 넣는거
			WaitForSingleObject(hMutex, INFINITE);
			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum] = refInfo;
			myNode.fileInfo.fileNum++;
			ReleaseMutex(hMutex);

			printf("CHORD> File Ref Info has been sent successfully to the Successor.\n");
			printf("CHORD> File Add has been successfully finished.\n");


			break;

		case 'd' :   // File Reference Info delete
			searchResult = 0;
			printf("CHORD> Input file name to Delete : ");
			fgets(fileName, sizeof(fileName), stdin);
			fgetsCleanup(fileName);

			targetKey = strHash(fileName);
			printf("CHORD> Input File Name: %s, Key: %d\n", fileName, targetKey);

			for (i = 0; i < myNode.fileInfo.fileNum; i++) {
				if (myNode.fileInfo.fileRef[i].Key == targetKey) {
					searchResult++;
					break;
				}
			}
			if (searchResult == 0) {
				printf("CHORD> File does not exist. File Search Failed\n");
				continue;
			}
			else {
				succNode = find_successor(myNode.nodeInfo, targetKey);
				for (j = i; j < myNode.fileInfo.fileNum - 1; j++)  // 본인에서 삭제
					myNode.fileInfo.fileRef[j] = myNode.fileInfo.fileRef[j + 1];
				myNode.fileInfo.fileNum--;

				peerAddr = succNode.addrInfo;
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 10;  // file Reference Delete
				tempMsg.msgType = 0;;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;

				// send a file reference add request message to the target node
				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(peerAddr));

				// receive a file reference add response message from the target node
				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

				if (bufMsg.fileInfo.Key == -1) {
					printf("CHORD> File %s Reference Info not exist!\n",fileName);
				}
				else {
					printf("CHORD> File %s delete Success!!\n",fileName);
					printf("CHORD> File %s Reference Info deleted from Succesoor Node %d\n", fileName, succNode.ID);
				}
			}
			break;
		case 's':
			frSock = socket(AF_INET, SOCK_STREAM, 0);   // 받아들일 소켓 생성
		
			check = 0;
			searchResult = 0;
			resultFlag = 0;
			numTotal = 0;
			WaitForSingleObject(hMutex, INFINITE);
			if (myNode.fileInfo.fileNum == FileMax) { // file number is full 
				printf("\a[ERROR] Your Cannot Add more file. File Space is Full!\n\n");
				continue;
			}
			ReleaseMutex(hMutex);
			// input the file name
			printf("CHORD> Input file name to search and download : ");
			fgets(fileName, sizeof(fileName), stdin);
			fgetsCleanup(fileName);

			targetKey = strHash(fileName);
			printf("CHORD> Input File Name: %s, Key: %d\n", fileName, targetKey);

			succNode = find_successor(myNode.nodeInfo, targetKey);

			if(myNode.fileInfo.fileNum >0 ) {
				for (i = 0; i < myNode.fileInfo.fileNum; i++) {
					if (myNode.fileInfo.fileRef[i].Key == targetKey) {
						printf("CHORD> The file %s is at this node itself\n", fileName);
						searchResult++;
					}
				}
			}
			if(searchResult==0) {
				peerAddr = succNode.addrInfo;
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 12;  // file Refence
				tempMsg.msgType = 0;;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;

				// send a file reference add request message to the target node
				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(peerAddr));

				// receive a file reference add response message from the target node
				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] FileReference Request timed out. FileReference Failed!\n");
						continue;
					}
					printf("\a[ERROR] FileReference Recvfrom Error.FileReference Failed!\n");
					continue;
				}

				if ((bufMsg.msgID != 12) || (bufMsg.msgType != 1)) { // wrong msg
					printf("\a[ERROR] Wrong Message (not FileReference) Received. FileReference Failed!\n");
					continue;
				}

				if (bufMsg.moreInfo == -1) { // failure
					printf("\a[ERROR] FileReference Request Failed. FileReference Failed!\n");
					continue;
				}
				if (bufMsg.fileInfo.Key == -1) {   // 아무것도 넘어온게 없을 때
					printf("CHORD> File's Successor IP Addr : %s, Port No : %d, ID : %d\n",inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);
					printf("CHORD> FileRefInfo does not exist. File Search Failed\n");
				}
				else {
					refInfo = bufMsg.fileInfo;
					printf("CHORD> File's Successor IP Addr : %s, Port No : %d, ID : %d\n", inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);
					printf("CHORD> File Owner IP Addr : %s, Port No : %d, ID : %d\n", inet_ntoa(bufMsg.fileInfo.owner.addrInfo.sin_addr), ntohs(bufMsg.fileInfo.owner.addrInfo.sin_port), bufMsg.fileInfo.owner.ID);
					check++;
				}
			}
			// Down File
			if (check > 0) {
				if (myNode.fileInfo.fileNum + 1 > FileMax)
					printf("\a[ERROR] My Node Cannot Add more file info. File Space is Full!\n");
				else {
					peerAddr = refInfo.owner.addrInfo;
					memset(&tempMsg, 0, sizeof(tempMsg));
					tempMsg.msgID = 11;  // file Down
					strcpy(tempMsg.fileInfo.Name, fileName);  
					tempMsg.msgType = 0;
					tempMsg.bodySize = 0;
					tempMsg.nodeInfo = myNode.nodeInfo;

					
					sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
						(struct sockaddr *) &peerAddr, sizeof(peerAddr));

					while (1) {
						// accept()
						addrSize = sizeof(peerAddr);
						frSock = accept(flSock, (SOCKADDR *)&peerAddr, &addrSize);
						if (frSock == INVALID_SOCKET) {
							perror("CHORD> accept()");
							continue;
						}
						
						// 파일 크기 받기
						retVal = recvn(frSock, (char *)&fileSize, sizeof(fileSize), 0);
						if (retVal == SOCKET_ERROR) {
							printf("CHORD> recv()\n");
							closesocket(frSock);
							continue;
						}

						// 파일 열기
						FILE *fp = fopen(fileName, "wb");
						if (fp == NULL) {
							printf("CHORD> 파일 입출력 오류\n");
							closesocket(frSock);
							continue;
						}
						// 파일 데이터 받기
						while (1) {
							retVal = recvn(frSock, fileBuf, fBufSize, 0);
							if (retVal == SOCKET_ERROR) {
								printf("CHORD> recv()\n");
								break;
							}
							else if (retVal == 0)
								break;
							else {
								fwrite(fileBuf, 1, retVal, fp);
								if (ferror(fp)) {
									perror("CHORD> 파일 입출력 오류");
									break;
								}
								numTotal += retVal;
							}
						}
						fclose(fp);

						// 복호화


						closesocket(frSock);
						break;
					}
					printf("CHORD> File %s has been received successfully!  File Size : %d\n", fileName, fileSize);
				}
			}

			break;
		case 'f':
			printf("CHORD> Finger table Information\n");
			printf("CHORD> My IP Addr : %s, Port No : %d, ID : %d\n", inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port), myNode.nodeInfo.ID);
			printf("CHORD> Predecessor IP Addr : %s, Port No : %d, ID : %d\n", inet_ntoa(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_port), myNode.chordInfo.fingerInfo.Pre.ID);
			for (i = 0; i < baseM; i++) {
				printf("CHORD> Finger[%d] IP Addr : %s, Port No: %d, ID : %d\n", i,inet_ntoa(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_port), myNode.chordInfo.fingerInfo.finger[i].ID);
			}
			break;
		case 'i':
			printf("CHORD> My Information:\n");
			printf("CHORD> My Node IP Addr: %s, Port No: %d, ID: %d \n", inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port), myNode.nodeInfo.ID);
			
			if (myNode.fileInfo.fileNum == 0 && myNode.chordInfo.FRefInfo.fileNum == 0)
				printf("CHORD> My Node has any files\n");
			for (i = 0; i < myNode.fileInfo.fileNum; i++) 
				printf("CHORD> %dth own file name: %s , key : %d\n",i+1,myNode.fileInfo.fileRef[i].Name, myNode.fileInfo.fileRef[i].Key);
			for (i = 0; i< myNode.chordInfo.FRefInfo.fileNum; i++) 
				printf("CHORD> %dth file ref name: %s , key : %d, owner ID : %d\n", i + 1, myNode.chordInfo.FRefInfo.fileRef[i].Name, myNode.chordInfo.FRefInfo.fileRef[i].Key,myNode.chordInfo.FRefInfo.fileRef[i].owner.ID);
			
			break;
		case 'l' :
			printf("CHORD> My Node leaveing\n");
			for (i = 0; i < myNode.fileInfo.fileNum; i++) {
				succNode = find_successor(myNode.nodeInfo, myNode.fileInfo.fileRef[i].Key);
				
				peerAddr = succNode.addrInfo;
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 10;  // file Delete
				tempMsg.msgType = 0;
				tempMsg.moreInfo = myNode.fileInfo.fileRef[i].Key;
				tempMsg.bodySize = 0;

				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(peerAddr));

				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			}
			myNode.fileInfo.fileNum = 0;  // 소유 파일 없앰
			
			succNode = myNode.chordInfo.fingerInfo.finger[0];

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++) {
				
				peerAddr = succNode.addrInfo;
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 8;  // leave Keys
				tempMsg.msgType = 0;
				tempMsg.bodySize = 0;
				tempMsg.fileInfo = myNode.chordInfo.FRefInfo.fileRef[i];

				// send a file reference add request message to the target node
				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(peerAddr));

				// receive a file reference add response message from the target node
				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			}
			myNode.chordInfo.FRefInfo.fileNum = 0;   // 위치정보 파일 없앰		

			cmdChar = 'q';

			break;

		case 'h' : 
			printf("CHORD>\n");
			showCommand();
		}
	} while (cmdChar != 'q');


	/* step 5: Program termination */
	
	exitFlag = 1;  
	printf("CHORD> Quitting the Program...\n");

	hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void *)procRecvMsg, (void *)&exitFlag, 0, NULL);
	hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void *)procPPandFF, (void *)&exitFlag, 0, NULL);

	WaitForMultipleObjects(2, hThread, TRUE, INFINITE);

	closesocket(rqSock);
	closesocket(rpSock);
	closesocket(frSock);
	closesocket(flSock);
	CloseHandle(hMutex);

	WSACleanup();

	printf("************************* B Y E*****************************\n");

	
	return 0;
}


void procRecvMsg(void *arg)
{
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeInfoType succNode, predNode, reqNode;

	int optVal = 5000; // 5 seconds
	int retVal; // return value

	fileInfoType keysInfo;
	char fileBuf[fBufSize] = {0,};
	FILE *fp;
	int i, j, targetKey, resultCode, keyNum, addrSize, fileSize, numRead, numTotal=0, searchresult =0;
	int *exitFlag = (int *)arg;

	addrSize = sizeof(peerAddr);

	while (!(*exitFlag)) {
		retVal = recvfrom(rpSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &peerAddr, &addrSize);

		if (retVal == SOCKET_ERROR)
			continue;
		if (bufMsg.msgType != 0) {
			printf("\a[ERROR] Unexpected Response Message Received. ThereforeMessage Ignored!\n");
			continue;
		}
		
		switch (bufMsg.msgID) {
		case 0: // PingPong  
			if (sMode == 0) {
				printf("CHORD> PingPong Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 0;  // PingPong
			tempMsg.msgType = 1; // reponse
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.finger[0];

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));
			if (sMode == 0)
				printf("CHORD> PingPong Response message success.\n");


			break;
		case 1: // Join Info
			if (sMode == 0) {
				printf("CHORD> Join Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			succNode = find_successor(myNode.nodeInfo, bufMsg.nodeInfo.ID);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 1;  // join info
			tempMsg.msgType = 1; // reponse
			tempMsg.nodeInfo = succNode;
			tempMsg.moreInfo = 0; // 단순한 결과


			retVal = sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Join info Response message success.\n");


			break;
		case 2: // Move Keys
			if (sMode == 0) {
				printf("CHORD> Move keys Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			searchresult = 0;

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++) {
				if (modIn(ringSize, bufMsg.nodeInfo.ID, myNode.chordInfo.FRefInfo.fileRef[i].Key, myNode.nodeInfo.ID, 1, 0)) {
					searchresult++;
					break;
				}
			}

			if (searchresult == 0) {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 2;  // Move Keys
				tempMsg.msgType = 1; // reponse
				tempMsg.moreInfo = 0; // 단순한 결과
				tempMsg.fileInfo.Key = -1;
			}
			else {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 2;  // Move Keys
				tempMsg.msgType = 1; // reponse
				tempMsg.moreInfo = 0; // 단순한 결과
				tempMsg.fileInfo = myNode.chordInfo.FRefInfo.fileRef[i];

				for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++)
					myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
				myNode.chordInfo.FRefInfo.fileNum--;
			}

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Move Keys Response message success.\n");

			break;
		case 3:  // Predecessor Info
			if (sMode == 0) {
				printf("CHORD> Predecessor Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 3;  // Pred info
			tempMsg.msgType = 1; // reponse
			tempMsg.moreInfo = 0; // 단순한 결과
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.Pre;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Predecessor Info Response message success.\n");

			break;
		case 4:  // Predecessor Update
			if (sMode == 0) {
				printf("CHORD>  Predecessor Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			myNode.chordInfo.fingerInfo.Pre = bufMsg.nodeInfo;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 4;  // Predecessor Update
			tempMsg.msgType = 1; // reponse
			tempMsg.moreInfo = 0; // 단순한 결과

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));
			if (sMode == 0)
				printf("CHORD> Predecessor Update Response message success.\n");

			break;
		case 5:  // successor Info
			if (sMode == 0) {
				printf("CHORD> sucessor Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;  // succ info
			tempMsg.msgType = 1; // reponse
			tempMsg.moreInfo = 0; // 단순한 결과
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.finger[0];

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Successor Info response message success.\n");
			break;
		case 6:  // Successor Update
			if (sMode == 0) {
				printf("CHORD> Successor Update Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			myNode.chordInfo.fingerInfo.finger[0] = bufMsg.nodeInfo;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 6;  // succ update
			tempMsg.msgType = 1; // reponse
			tempMsg.moreInfo = 0; // 단순한 결과

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Suc Update response message success.\n");

			break;
		case 7:  // Find predecessor
			if (sMode == 0) {
				printf("CHORD> Find predecessor Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			predNode = find_predecessor(myNode.nodeInfo, bufMsg.moreInfo);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 7;  // Find predecessor
			tempMsg.msgType = 1; // reponse
			tempMsg.moreInfo = 0; // 단순한 결과
			tempMsg.nodeInfo = predNode;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Find predecessor response message success.\n");

			break;
		case 8:  // Leave Keys
			if (sMode == 0) {
				printf("CHORD> Leave Keys Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum] = bufMsg.fileInfo;
			myNode.chordInfo.FRefInfo.fileNum++;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 8;  // Leave Keys
			tempMsg.msgType = 1; // reponse
			tempMsg.moreInfo = 0; // 단순한 결과

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sock_addr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> Leave Keys Response Message has been sent.\n");
			break;
		case 9: // File Ref Add
			resultCode = 0;
			if (sMode == 0) {
				printf("CHORD> File Reference Add Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			if (myNode.chordInfo.FRefInfo.fileNum == FileMax) { // file ref number is full 
				printf("\a[ERROR] My Node Cannot Add more file ref info. File Ref Space is Full!\n");
				resultCode = -1;
			}

			WaitForSingleObject(hMutex, INFINITE);
			// file ref info update
			myNode.chordInfo.FRefInfo.fileRef[myNode.chordInfo.FRefInfo.fileNum] = bufMsg.fileInfo;
			myNode.chordInfo.FRefInfo.fileNum++;
			ReleaseMutex(hMutex);

			if (sMode == 0) {
				printf("CHORD> File Reference Information has been updated.\n");
				printf("CHORD> File Name: %s, Owner ID: %d\n", bufMsg.fileInfo.Name, bufMsg.fileInfo.owner.ID);
			}

			// create FileRefAdd response message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 9;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			// send a PredUpdate response message to the asking node
			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> FileRefAdd Response Message has been sent.\n");

			break;
		case 10:  // File Reference Delete
			searchresult = 0;
			if (sMode == 0) {
				printf("CHORD> File Reference Delete Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			
			WaitForSingleObject(hMutex, INFINITE);
			// file ref info update

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++) {
				if (myNode.chordInfo.FRefInfo.fileRef[i].Key == bufMsg.moreInfo) {
					searchresult++;
					break;
				}
			}

			if (searchresult == 0) {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 10;  // Pred info
				tempMsg.msgType = 1; // reponse
				tempMsg.moreInfo = 0; // 단순한 결과
				tempMsg.fileInfo.Key = -1;
			}
			else {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 10;  // Pred info
				tempMsg.msgType = 1; // reponse
				tempMsg.moreInfo = 0; // 단순한 결과

				for (j = i; j < myNode.chordInfo.FRefInfo.fileNum - 1; j++)
					myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
				myNode.chordInfo.FRefInfo.fileNum--;
			}

			ReleaseMutex(hMutex);

			if (sMode == 0) {
				printf("CHORD> File Delete has been updated.\n");
			}

			// send a PredUpdate response message to the asking node
			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0)
				printf("CHORD> File Reference Delete Response Message has been sent.\n");

			break;
		case 11: // FileDown
			if (sMode == 0) {
				printf("CHORD> File Down Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			searchresult = 0;
			numTotal = 0;
		
			WaitForSingleObject(hMutex, INFINITE);
			
			for (i = 0; i < myNode.fileInfo.fileNum; i++) {
				if (!strcmp(myNode.fileInfo.fileRef[i].Name, bufMsg.fileInfo.Name)) 
					break;
			}

			fsSock = socket(AF_INET, SOCK_STREAM, 0);   // 소켓 생성
			if (fsSock == INVALID_SOCKET) printf("socket()");
			retVal = connect(fsSock, (SOCKADDR *)&bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));
			if (retVal == SOCKET_ERROR) printf("connect()\n");


			fp = fopen(myNode.fileInfo.fileRef[i].Name, "rb");
			fseek(fp, 0, SEEK_END);
			fileSize = ftell(fp);

			retVal = send(fsSock, (char *)&fileSize,sizeof(fileSize), 0);   // 또 보낸다
			if (retVal == SOCKET_ERROR) printf("send()");

			rewind(fp);
			while (1) {
				numRead = fread(fileBuf, 1, fBufSize, fp);
				if (numRead > 0) {
					retVal = send(fsSock, fileBuf, numRead, 0);
					if (retVal == SOCKET_ERROR) {
						printf("send()");
						break;
					}
					numTotal += numRead;
				}
				else if (numRead == 0 && numTotal == fileSize) {
					printf("CHORD> 파일 전송 완료! %d 바이트\n", numTotal);
					break;
				}
				else {
					perror("CHORD> 파일 입출력 오류");
					break;

				}
			}

			ReleaseMutex(hMutex);

			if (sMode == 0)
				printf("CHORD> FileDown Response Message has been sent.\n");
			fclose(fp);
			closesocket(fsSock);

			break;

		case 12:  // File Reference Info
			searchresult = 0;
			if (sMode == 0) {
				printf("CHORD> File Reference Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			WaitForSingleObject(hMutex, INFINITE);
			// 검사하는 구간

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++) {
				if (myNode.chordInfo.FRefInfo.fileRef[i].Key == bufMsg.moreInfo) {
					searchresult++;
					break;
				}
			}
			ReleaseMutex(hMutex);

			if (sMode == 0) {
				printf("CHORD> File Reference Information has been updated.\n");
				printf("CHORD> File Name: %s, Owner ID: %d\n", bufMsg.fileInfo.Name, bufMsg.fileInfo.owner.ID);
			}

			// create FileRef response message
			if (searchresult == 0) {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 12;
				tempMsg.msgType = 1;
				tempMsg.fileInfo.Key = -1;
				tempMsg.moreInfo = 0;
				tempMsg.bodySize = 0;
			}
			else {
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 12;
				tempMsg.msgType = 1;
				tempMsg.fileInfo = myNode.chordInfo.FRefInfo.fileRef[i];
				tempMsg.moreInfo = 0;
				tempMsg.bodySize = 0;
			}

			// send a PredUpdate response message to the asking node
			
			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(peerAddr));


			if (sMode == 0)
				printf("CHORD> FileReference Info Response Message has been sent.\n");

			break;
		}


		if (sMode == 0) {
			printf("CHORD> \n");
			printf("CHORD> ");
		}
	}
}


void procPPandFF(void *arg)
{
	int *exitFlag = (int *)arg;
	unsigned int delayTime, varTime;
	int retVal, optVal = 5000;
	int i, j, targetKey, check = 0,check1 =0;
	struct sockaddr_in peerAddr;
	nodeInfoType tempNode, tempNode2, succNode;
	chordHeaderType tempMsg, bufMsg;
	srand(time(NULL));
	pfSock = socket(AF_INET, SOCK_DGRAM, 0); // for ping-pong and fix-finger
	retVal = setsockopt(pfSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));

	if (retVal == SOCKET_ERROR) {
		printf("\a[ERROR] setsockopt() Error!\n");
		exit(1);
	}

	while (!(*exitFlag)) {
		varTime = rand() % 2000;
		delayTime = 8000 + varTime; // delay: 8~10 sec
		Sleep(delayTime);
		check = 0;
		check1 = 0;
		tempNode.ID = -1;
		// Pre 먼저 보내고
		memset(&tempMsg, 0, sizeof(tempMsg));
		tempMsg.msgID = 0;  // pingpong
		tempMsg.msgType = 0; // request
		tempMsg.moreInfo = 0; // 단순한 결과
		sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.Pre.addrInfo,
			sizeof(myNode.chordInfo.fingerInfo.Pre.addrInfo));

		retVal = recvfrom(pfSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
	
		if (retVal == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT) {
				printf("\a[ERROR] Pingpong timed out. Pingpong Failed!\n");
				continue;
			}
			//printf("\a[ERROR] Pingpong Recvfrom Error. Pingpong Failed!\n");
			tempNode = myNode.chordInfo.fingerInfo.Pre;
			check++;
		}

		// finger로 보냄
		for (i = 0; i < baseM; i++) {
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 0;  // pingpong
			tempMsg.msgType = 0; // request
			tempMsg.moreInfo = 0; // 단순한 결과
			sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[i].addrInfo,
				sizeof(myNode.chordInfo.fingerInfo.finger[i].addrInfo));

			retVal = recvfrom(pfSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Pingpong timed out. Pingpong Failed!\n");
					continue;
				}
				tempNode = myNode.chordInfo.fingerInfo.finger[i];
				check1++;
			}
			else {   // 올바르게 값이 들어올때 
				if (i == 0)
					succNode = bufMsg.nodeInfo;
			}

			if ((bufMsg.msgID != 0) || (bufMsg.msgType != 1)) { // wrong msg
				printf("\a[ERROR] Wrong Message (not Pingpong) Received. Pingpong Failed!\n");
				continue;
			}
		}
		
		if (tempNode.ID != -1) {
			printf("\nCHORD> IP : %s, Port No : %d, ID : %d 가 떠났습니다.\n", inet_ntoa(tempNode.addrInfo.sin_addr), ntohs(tempNode.addrInfo.sin_port), tempNode.ID);
			if ((check + check1) == 0)
				printf("CHORD> 변경되는 값이 없습니다.\n");
		}
		if (check > 0)
			printf("CHORD> Pre 값이 변경됩니다\n");
		if (check1 > 0)
			printf("CHORD> finger table %d 개의 값이 변경됩니다\n",check1);

		if ((check + check1) == 7) {   // 모든 값이 고쳐져야 할때
			myNode.chordInfo.fingerInfo.Pre = myNode.nodeInfo;
			for (i = 0; i < baseM; i++) {
				myNode.chordInfo.fingerInfo.finger[i] = myNode.nodeInfo;
			}
			printf("CHORD> \n");
			printf("CHORD> Enter your command ('help' for help message).\n");
			printf("CHORD> ");
		}
		else {
			if (check > 0 || check1 > 0) {
				if (check > 0) {  // pre 가 비었다
					if (myNode.chordInfo.fingerInfo.finger[baseM - 1].ID == tempNode.ID) {   // 끝값도 빈것을 확인
						for (i = baseM - 2; i >= 0; i--) {   // 가장 근처의 값을 찾음
							if (myNode.chordInfo.fingerInfo.finger[i].ID != tempNode.ID) {
								tempNode2 = myNode.chordInfo.fingerInfo.finger[i];
								break;
							}
						}
						myNode.chordInfo.fingerInfo.finger[baseM - 1] = tempNode2;
						myNode.chordInfo.fingerInfo.Pre = tempNode2;
					}
					else {					    // Pre 비었는데 끝값은 있음
						myNode.chordInfo.fingerInfo.Pre = myNode.chordInfo.fingerInfo.finger[baseM - 1];
					}
				}
				else {   // Pre 가 있는데 끝값이 없을 경우
					if (myNode.chordInfo.fingerInfo.finger[baseM - 1].ID == tempNode.ID)
						myNode.chordInfo.fingerInfo.finger[baseM - 1] = myNode.chordInfo.fingerInfo.Pre;
				}
				
				for (i = baseM - 2; i >= 0; i--) {
					if (myNode.chordInfo.fingerInfo.finger[i].ID == tempNode.ID) {
						if (i == 0) {    // 첫째 값이 변경될 경우 Leave Node의 sucNode를 줌
							myNode.chordInfo.fingerInfo.finger[0] = succNode;
						}
						else {
							myNode.chordInfo.fingerInfo.finger[i] = myNode.chordInfo.fingerInfo.finger[i + 1];
						}
					}
				}
				// 핑거테이블을 모두 채워준 후
				for (i = 0; i < baseM; i++) {
					memset(&tempMsg, 0, sizeof(tempMsg));
					tempMsg.msgID = 3;  // Pred info
					tempMsg.msgType = 0; // request
					tempMsg.moreInfo = 0; // 단순한 결과

					tempNode2 = myNode.chordInfo.fingerInfo.finger[i];

					sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &tempNode2.addrInfo, sizeof(tempNode2.addrInfo));

					retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

					if (bufMsg.nodeInfo.ID == tempNode.ID && i==0)  // 본인의 후임을 찾음
						break;
				}
				if (i != baseM) {   // 후임의 Pre를 update 함
					memset(&tempMsg, 0, sizeof(tempMsg));
					tempMsg.msgID = 4;  // pre Update
					tempMsg.msgType = 0; // request
					tempMsg.moreInfo = 0; // 단순한 결과
					tempMsg.nodeInfo = myNode.nodeInfo;

					sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &tempNode2.addrInfo, sizeof(tempNode2.addrInfo));

					retVal = recvfrom(rqSock, (char*)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				}
				printf("CHORD> \n");
				printf("CHORD> Enter your command ('help' for help message).\n");
				printf("CHORD> ");
			}
		}
		 // 나머지 값들은 fix_finger 로 해결
		fix_finger(myNode.nodeInfo);
	}
	return 0;
}


nodeInfoType find_successor(nodeInfoType curNode, int IDKey)
{
	int retVal;
	int addrSize;
	chordHeaderType sendMsg, recvMsg;
	nodeInfoType preNode;

	if (curNode.ID == IDKey) {
		return curNode;
	}
	else {
		preNode = find_predecessor(curNode, IDKey);
		if (preNode.ID == curNode.ID) {    // 다시 생각해볼 것
			return myNode.chordInfo.fingerInfo.finger[0];      
		}

		memset(&sendMsg, 0, sizeof(sendMsg));
		sendMsg.msgID = 5; //success Info 요청
		sendMsg.msgType = 0;
		sendMsg.moreInfo = 0;
		sendto(rqSock, (char *)&sendMsg, sizeof(sendMsg), 0,(struct sockaddr *) &preNode.addrInfo, sizeof(preNode.addrInfo));
		

		addrSize = sizeof(preNode.addrInfo);
		retVal = recvfrom(rqSock, (char *)&recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&preNode.addrInfo, &addrSize);
		
		if (retVal == SOCKET_ERROR) {
			printf("\a[ERROR] setsockopt() Error!\n");
			exit(1);
		}
		return recvMsg.nodeInfo;		
	}

	printf("find_successor OK\n");
}

nodeInfoType find_predecessor(nodeInfoType curNode, int IDKey)
{
	int i = 0;
	int retVal;
	int addrSize;
	chordHeaderType sendMsg, recvMsg;
	nodeInfoType fingerNode;
	nodeInfoType tempNode = curNode;
	if (tempNode.ID == myNode.chordInfo.fingerInfo.finger[0].ID) { // special case: the initial node
		return tempNode;
	}
	if (!modIn(ringSize, IDKey, tempNode.ID, myNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)) {
		for (i = baseM - 1; i >= 0; i--) {
			if (myNode.chordInfo.fingerInfo.finger[i].ID == 0)
				continue;
			if (modIn(ringSize, myNode.chordInfo.fingerInfo.finger[i].ID, myNode.nodeInfo.ID, IDKey, 0, 0))
				fingerNode = myNode.chordInfo.fingerInfo.finger[i];
		}

		memset(&sendMsg, 0, sizeof(sendMsg));
		sendMsg.msgID = 7; //find predecessor 요청
		sendMsg.msgType = 0;
		sendMsg.moreInfo = IDKey;
		sendto(rqSock, (char *)&sendMsg, sizeof(sendMsg), 0, (struct sockaddr *) &fingerNode.addrInfo, sizeof(fingerNode.addrInfo));


		addrSize = sizeof(fingerNode.addrInfo);
		retVal = recvfrom(rqSock, (char *)&recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&fingerNode.addrInfo, &addrSize);
		//printf("최종적으로 받아오는 값 : %d", recvMsg.nodeInfo.ID);
		if (retVal == SOCKET_ERROR) {
			printf("\a[ERROR] setsockopt() Error!\n");
			exit(1);
		}
		return recvMsg.nodeInfo;
	}
	else
		return tempNode;
}

int fix_finger(nodeInfoType curNode)
{
	int i;

	for (i = 1; i < baseM; i++) 
		myNode.chordInfo.fingerInfo.finger[i] = find_successor(curNode, modPlus(ringSize, curNode.ID, twoPow(i)));

	return 0;
}