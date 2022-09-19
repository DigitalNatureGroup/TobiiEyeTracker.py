#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <tchar.h>

#include <tobii.h>
#include <tobii_streams.h>

#include <WinSock2.h>// WinSocketを使用するためのInclude
#include <WS2tcpip.h>// WinSocketを使用するためのInclude

#pragma comment(lib, "ws2_32.lib")

#define ADDRESS "127.0.0.1"
#define PORT 1235

#define OUTPUT_BUFFER_SIZE 1024


static FILE *fp_tobii_eye, *fp_tobii_eyep, *fp_tobii_headp;

int eyedata;
int eyepositiondata_left, eyepositiondata_right;
double wink_left, wink_right;
int headpositiondata;

float eye_z_left, eye_z_right;

int64_t base_time;
time_t start_time;

int sock;
struct sockaddr_in addr;

void sendUDPMessage(int64_t now, float x, float y)
{
	char data[50];
	memset(data, 0, sizeof(data));
	size_t size = sprintf_s(data, sizeof(data), "%lld,%f,%f", now, x, y);

	sendto(sock, data, size, 0, (struct sockaddr *)&addr, sizeof(addr));
}

void head_pose_callback(tobii_head_pose_t const* head_pose, void* user_data)
{
	int64_t elapsed = head_pose->timestamp_us - base_time;
	if (head_pose->position_validity == TOBII_VALIDITY_VALID) {
		fprintf(fp_tobii_headp, "%lld,%f,%f,%f,",
			elapsed,
			head_pose->position_xyz[0],
			head_pose->position_xyz[1],
			head_pose->position_xyz[2]);

		if (headpositiondata == 0) {
			headpositiondata = 1;
		}
		printf("Position:,%f,%f,%f,",
			head_pose->position_xyz[0],
			head_pose->position_xyz[1],
			head_pose->position_xyz[2]);
	}
	else {
		fprintf(fp_tobii_headp, "%lld,-1,-1,-1,", elapsed);

		if (headpositiondata == 1) {
			headpositiondata = 0;
		}
	}

	if (head_pose->position_validity == TOBII_VALIDITY_VALID) {
		fprintf(fp_tobii_headp, "%f,%f,%f\n",
			head_pose->rotation_xyz[0],
			head_pose->rotation_xyz[1],
			head_pose->rotation_xyz[2]);
		printf("Rotation:,%f,%f,%f\n",
			head_pose->rotation_xyz[0],
			head_pose->rotation_xyz[1],
			head_pose->rotation_xyz[2]);
	}
	else {
		fprintf(fp_tobii_headp, "-1,-1,-1\n");
	}
}


void eye_position_callback(tobii_gaze_origin_t const* eye_pos, void* user_data)
{
	int64_t elapsed = eye_pos->timestamp_us - base_time;

	if (eye_pos->left_validity == TOBII_VALIDITY_VALID) {
		fprintf(fp_tobii_eyep, "%lld,%f,%f,%f,",
		elapsed,
		eye_pos->left_xyz[0],
		eye_pos->left_xyz[1],
		eye_pos->left_xyz[2]);
		eye_z_left = eye_pos->left_xyz[2];
	}
	else {
		fprintf(fp_tobii_eyep, "%lld,-1,-1,-1,", elapsed);
		if (eyepositiondata_left == 1) {
			eyepositiondata_left = 0;
		}
	}

	if (eye_pos->right_validity == TOBII_VALIDITY_VALID) {

		fprintf(fp_tobii_eyep, "%f,%f,%f\n",
		eye_pos->right_xyz[0],
		eye_pos->right_xyz[1],
		eye_pos->right_xyz[2]);
		eye_z_right = eye_pos->right_xyz[2];
	}
	else {
		fprintf(fp_tobii_eyep, "-1,-1,-1\n");
		if (eyepositiondata_right == 1) {
			eyepositiondata_right = 0;
		}
	}
}


static void gaze_point_callback(tobii_gaze_point_t const* gaze_point, void* user_data)
{
	int64_t elapsed = gaze_point->timestamp_us - base_time;

	double x, y;
	if (gaze_point->validity == TOBII_VALIDITY_VALID) {
		fprintf(fp_tobii_eye, "%lld,%f,%f\n",
			elapsed,
			gaze_point->position_xy[0],
			gaze_point->position_xy[1]);

		x = double(gaze_point->position_xy[0]);
		y = double(gaze_point->position_xy[1]);

		printf("%lld: %f,%f / %f,%f\n",
			elapsed,
			gaze_point->position_xy[0],
			gaze_point->position_xy[1], eye_z_left, eye_z_right);

		sendUDPMessage(start_time * 1000000 + elapsed, x, y);
	}
	else {
		fprintf(fp_tobii_eye, "%lld,-1,-1\n", elapsed);
	}
}


static void url_receiver(char const* url, void* user_data)
{
	char* buffer = (char*)user_data;
	if (*buffer != '\0') return; // only keep first value

	if (strlen(url) < 256)
		strcpy(buffer, url);
}


int main()
{
  WSAData wsaData;
//   struct timeval tv;

  WSACleanup();
  WSAStartup(MAKEWORD(2, 0), &wsaData);   //MAKEWORD(2, 0)はwinsockのバージョン2.0ってこと

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);// 待ち受けポート番号を50008にする
  // addr.sin_addr.S_un.S_addr = inet_addr(ADDRESS);// 送信アドレスを設定
  InetPton(addr.sin_family, _T(ADDRESS), &addr.sin_addr.S_un.S_addr);

	char filename[3][50];

	sprintf_s(filename[0], sizeof(filename[0]), "gaze_point.csv");
	sprintf_s(filename[1], sizeof(filename[1]), "eye_position.csv");
	sprintf_s(filename[2], sizeof(filename[2]), "head_position.csv");
	fopen_s(&fp_tobii_eye, filename[0], "w");
	fopen_s(&fp_tobii_eyep, filename[1], "w");
	fopen_s(&fp_tobii_headp, filename[2], "w");

	eyedata = 0;
	eyepositiondata_left = 0;
	eyepositiondata_right = 0;
	wink_left = 0;
	wink_right = 0;
	headpositiondata = 0;

	tobii_api_t* api;
	tobii_error_t error = tobii_api_create(&api, NULL, NULL);
	assert(error == TOBII_ERROR_NO_ERROR);

	char url[256] = { 0 };
	error = tobii_enumerate_local_device_urls(api, url_receiver, url);
	assert(error == TOBII_ERROR_NO_ERROR && *url != '\0');

	tobii_device_t* device;
	error = tobii_device_create(api, url, TOBII_FIELD_OF_USE_INTERACTIVE, &device);
	assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_system_clock( api, &base_time );
	if( error == TOBII_ERROR_NO_ERROR ) {
		start_time = time(NULL);
		printf( "start at %lld\n", start_time);
	}

	// error = tobii_head_pose_subscribe(device, head_pose_callback, 0);
	// assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_gaze_origin_subscribe(device, eye_position_callback, 0);
	assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_gaze_point_subscribe(device, gaze_point_callback, 0);
	assert(error == TOBII_ERROR_NO_ERROR);

	while(1) {
		error = tobii_wait_for_callbacks(1, &device);
		assert(error == TOBII_ERROR_NO_ERROR || error == TOBII_ERROR_TIMED_OUT);

		error = tobii_device_process_callbacks(device);
		assert(error == TOBII_ERROR_NO_ERROR);
	}
		
	error = tobii_gaze_point_unsubscribe(device);
	assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_device_destroy(device);
	assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_api_destroy(api);
	assert(error == TOBII_ERROR_NO_ERROR);

	fclose(fp_tobii_eye);
	fclose(fp_tobii_eyep);
	fclose(fp_tobii_headp);

  closesocket(sock);
  WSACleanup();

	return 1;
}
