#pragma once
// Host-only JNI boundary for compiling the actual Android orchestration source.
#include <string>
#define JNIEXPORT
#define JNICALL
#define JNI_TRUE 1
#define JNI_FALSE 0
using jclass = void*;
using jobject = void*;
using jmethodID = void*;
using jstring = const char*;
using jint = int;
using jboolean = unsigned char;
struct JNIEnv {
    std::string text;
    jstring NewStringUTF(const char* s) { text = s; return text.c_str(); }
    jclass FindClass(const char*) { return nullptr; }
    jmethodID GetMethodID(jclass, const char*, const char*) { return nullptr; }
    jobject NewObject(jclass, jmethodID, ...) { return nullptr; }
    const char* GetStringUTFChars(jstring s, void*) { return s; }
    void ReleaseStringUTFChars(jstring, const char*) {}
};
