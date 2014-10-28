#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "wrapper_jni.h"
#include "defs.h"
#include "lock.h"
#include "fifo_char.h"
#include "util.h"

#define CLASS_NAME "com/example/jni/LibWrapper"
#define CALLBACK_METHOD_NAME "OnMessage"
#define FIFO_SIZE 4096

extern lock_t lock_buffer;

static jmethodID mSendStr;
static jclass jNativesCls;
static JavaVM *g_VM;

static char _dataDirectory[512];
static char _cacheDirectory[512];

//static fifo_t _queue;
//static char _input_buffer[4096];

static charfifo *pfifo;

struct zip* APKArchive;

static void loadAPK (const char* apkPath) {

	int i, numFiles;
	const char* name;

	LOGI("Loading APK %s", apkPath);

	APKArchive = zip_open(apkPath, 0, NULL);
	if (APKArchive == NULL) {
		LOGE("Error loading APK");
		return;
	}

	//Just for debug, print APK contents
	numFiles = zip_get_num_files(APKArchive);
	for (i=0; i<numFiles; i++) {
		name = zip_get_name(APKArchive, i, 0);
		if (name == NULL) {
			LOGE("Error reading zip file name at index %i : %s", i, zip_strerror(APKArchive));
			return;
		}
		LOGI("File %i : %s\n", i, name);
	}

}

/*
 * Output to java layer
 */
static void jni_send_str(const char * text) {
	JNIEnv *env;

	if (!g_VM) {
		return;
	}

	(*g_VM)->AttachCurrentThread(g_VM, &env, 0);

	if (!jNativesCls) {
		jNativesCls = (*env)->FindClass(env, CLASS_NAME);
		if (jNativesCls == 0) {
			return;
		}
	}

	if (!mSendStr) {
		mSendStr = (*env)->GetStaticMethodID(env, jNativesCls
				, CALLBACK_METHOD_NAME
				, "(Ljava/lang/String;)V");

		if(mSendStr == 0)
			return;
	}

	(*env)->CallStaticVoidMethod(env, jNativesCls
			, mSendStr
			, (*env)->NewStringUTF(env, text) );

}

static void unpack_files() {

	// open manifest file and read each file
	// check if file exists in data directory
	// extract file if it doesn't exist

	// 1. unpack .rc file and book files
	// 2. unpack and extract table database
	// 3. create version file and write current version

	char * rcfilename = "assets/data/crafty.rc";
	char rcdestination[512];
	int fd;

	strlcpy(rcdestination, _dataDirectory, sizeof(rcdestination));
	strlcat(rcdestination, "/crafty.rc", sizeof(rcdestination));
	LOGI("extracting %s to %s", rcfilename, rcdestination);
	if(extract_file(rcfilename, rcdestination) == 0) {
		fd = open(rcdestination, O_RDONLY, 0);
		if(fd > 0) {
			// check file
			LOGI("extract successful!");
		}
		close(fd);
	}
}

/* wrapper API */
void jni_printf(const char *format, va_list argptr) {
	static char string[1024];
	vsnprintf (string, 1023, format, argptr);
	jni_send_str(string);
}

void native_send(const char*fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	jni_printf(fmt, ap);
	va_end(ap);
}

int check_buffer() {
	int retval;

	Lock(lock_buffer);
	retval = !fifo_char_empty((void **)&pfifo);
	Unlock(lock_buffer);

	return retval;
}

int read_buffer(char * pz, int size) {
	Lock(lock_buffer);
	int bytes = fifo_char_read((void **)&pfifo, pz, size);
	Unlock(lock_buffer);
	return bytes;
}

void native_sound(const char* sound_name) {
}


/*
 * Input from java layer
 */
JNIEXPORT void JNICALL Java_com_example_jni_LibWrapper_SendMessage
  (JNIEnv * env, jclass jc, jstring js) {

	const char *nativeString = (*env)->GetStringUTFChars(env, js, 0);

	Lock(lock_buffer);
	fifo_char_write((void **)&pfifo, nativeString, strlen(nativeString));
	Unlock(lock_buffer);

	LOGI("Received string: %s, buffer size is now: %d", nativeString, fifo_char_size((void **)&pfifo));

	(*env)->ReleaseStringUTFChars(env, js, nativeString);
}


jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    g_VM = vm;

    if ((*g_VM)->GetEnv(g_VM, (void **)&env, JNI_VERSION_1_2) != JNI_OK) {
        return -1;
    }

    jclass localClass = (*env)->FindClass(env, CLASS_NAME);
    jNativesCls = (jclass)((*env)->NewGlobalRef(env, localClass));
	if (jNativesCls == 0) {
		return -1;
	}

	mSendStr = (*env)->GetStaticMethodID(env, jNativesCls
			, CALLBACK_METHOD_NAME
			, "(Ljava/lang/String;)V");

	//fifo_init(&_queue, _input_buffer, FIFO_SIZE + 1);

	fifo_char_create((void **)&pfifo, FIFO_SIZE + 1);
	LOGI("Fifo queue initialized with size %d", FIFO_SIZE);

    return JNI_VERSION_1_2;
}

/*
 * Initialize native layer
 */
JNIEXPORT void JNICALL Java_com_example_jni_LibWrapper_NativeInit
  (JNIEnv * env, jclass jc, jstring apkPath, jstring cache, jstring data)
{
	static int initialized = 0;

	if(!initialized) {
		const char* str;
		str = (*env)->GetStringUTFChars(env, apkPath, 0);
		loadAPK(str);

		initialized = 1;
		native_send("Initialization complete!  Apk Directory: %s\r\n", str, 4096);

		(*env)->ReleaseStringUTFChars(env, apkPath, str);

		str = (*env)->GetStringUTFChars(env, cache, 0);
		strlcpy(_cacheDirectory, str, sizeof(_cacheDirectory));
		(*env)->ReleaseStringUTFChars(env, cache, str);

		str = (*env)->GetStringUTFChars(env, data, 0);
		strlcpy(_dataDirectory, str, sizeof(_dataDirectory));
		(*env)->ReleaseStringUTFChars(env, data, str);

		LOGI("Data Directory : %s\nCache Directory : %s\n", _dataDirectory, _cacheDirectory);
		unpack_files();
	}
}

