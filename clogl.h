/* 
 * C语言日志记录
 * 可以多线程, 可以日志分级, 可以设置记录级别, 可以输出到多个方向
 * WARN!!! -> 初始化过程可不是线程安全的
 *
 * 作者:
 *    刘恒(liuhengloveyou@gmail.com)
 * 时间:
 *    2011.10.20
 */
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <syscall.h>

#ifndef CLOGL_H
#define CLOGL_H

#if defined (__cplusplus)
extern "C" {
#endif /* defined (__cplusplus) */

#define CLOGL_EVENT_TIME      100                                               // 事件触发间隔毫秒数
#define CLOGL_MSG_MAX         (512 * 1024)                                      // 日志信息最大长度(字节)
#define CLOGL_SRC_INFO        1                                                 // 日志信息里是否显示原代码文件信息
 
/*
 * 日志级别
 */
typedef enum
{
	CLOGL_LEVEL_DATA,
	CLOGL_LEVEL_ERR,
	CLOGL_LEVEL_WARN,
	CLOGL_LEVEL_INFO,
	CLOGL_LEVEL_DEBUG,
	CLOGL_LEVEL_UNKNOWN
} clogl_level;

/*
 * 日志输出目的地类型
 */
typedef enum
{
	CLOGL_APD_CONSOLE,                        /* 控制台 */
	CLOGL_APD_TIMEFILE,                       /* 按时间产生新的日志文件. 单位小时 */
	CLOGL_APD_SIZEFILE,                       /* 按文件大小产生新的文件. 单位兆 */
	CLOGL_APD_NET                             /* 发送到网络 */
} clogl_apd_type;

/* 
 * 按文件大小产生新的文件输出类型的属性
 */
typedef struct _clogl_apd_sizefile_opt
{
	char *fileName;                   // 日志文件名
	FILE *fp;                         // 当前打开日志文件的指针
	int maxSize;                      // 日志文件最大兆数
} cloglSizeFileOpt;

/*
 * 按时间产生新的日志文件输出类型的属性
 */
typedef struct _clogl_apd_timefile_opt
{
	char *fileName;                   // 日志文件名
	FILE *fp;                         // 当前打开日志文件的指针
	time_t span;                      // 间隔秒数. 从小时转成秒
	time_t now;                       // 当前日志文件产生的时间戳
} cloglTimeFileOpt;

struct _clogl_apd;
struct _clogl_logger;
	
/*
 * 一个日志格式
 */
typedef struct _clogl_fmt
{
	char *name; // 格式名
	char *(*format)(struct _clogl_logger *log, const char *format, va_list args);  // 格式化函数
} cloglFmt;

/*
 * 一个输出方向类型定义
 */
typedef struct _clogl_apd_type
{
	char *name;
	int (*open)(struct _clogl_apd*);
	int (*append)(struct _clogl_apd*, const char*);
	int (*close)(struct _clogl_apd*);
	int (*event)(struct _clogl_apd*);
} cloglApdT;

/*
 * 代表一个日志输出方向
 */
typedef struct _clogl_apd
{
	char *name;                   // 输出对象名
	int priority;                 // 输出级别
	int isOpen;                   // 是否已经打开
	cloglApdT *apdType;           // 一个类型的输出方向
	cloglFmt *fmt;                // 该输出方向的格式
	void *opt;                    // 不同类型输出方向的属性
	pthread_mutex_t  pLock;       // 线程锁
	struct _clogl_apd *next;
} cloglApd;

/*
  日志缓冲区结构
 */
typedef struct _clog_msg
{
	char *msgBuff;                // 输出日志信息缓冲区
	size_t msgSize;               // 当前日志信息缓冲区大小
} clogMsg;
	
/*
 * 代表一个日志对象
 */
typedef struct _clogl_logger
{
	char *name;                   // 日志对象名称
	int priority;                 // 输出级别
	cloglApd *apds;               // 多个输出方向
	pthread_key_t msgp;           // 日志缓冲区,各线程独立
	struct _clogl_logger *next;
} clogl_t;

/*
 * 功能:
 *    初始化日志模块
 * 入参:
 *    NO
 * 出参:
 *    NO
 * 返回值:
 *    -1 OR 0
 */
int cloglInit();

/*
 * 功能:
 *    跟据配置文件里的日志名，获得一个日志对象指针
 * 入参:
 *    日志对象名
 * 出参:
 *    NO
 * 返回值:
 *    成功返回日志对象指针, 出错返回 NULL
 */
clogl_t *cloglGet(const char *name);

/*
 * 功能:
 *    获得一个系统默认日志对象指针
 * 入参:
 *    NO
 * 出参:
 *    NO
 * 返回值:
 *    成功返回日志对象指针, 出错返回 NULL
 */
clogl_t *cloglGetDft();

/*
 * 功能:
 *    设置一个日志对象的输出级别
 * 入参:
 *    log: 日志对象
 *    p: 输出级别
 * 出参:
 *    NO
 * 返回值:
 *    0 OR -1
 */
int setLogPriority(clogl_t *log, int p);

/*
 * 功能:
 *    给用户调用的记录日志函数
 * 入参:
 *    log:      日志结构对象
 *    priority: 日志级别
 *    format:   日志信息
 * 出参:
 *    NO
 * 返回值:
 *    NO
 */
void clogLogger(clogl_t *log, int priority, const char *format, ...);

/*
 * 功能：
 *    日志级别从字符串转换为clogl_level
 * 入参：
 *    字符串形式的日志级别
 * 出参：
 *    NO
 * 返回值：
 *     clogl_level形式的日志级别
 */
clogl_level cloglLevel(const char *lvl);

#if defined (CLOGL_SRC_INFO)
#define CLOGL_DATA(logger, format, args...)  clogLogger(logger, CLOGL_LEVEL_DATA,  "[DATA] " format, ##args);
#define CLOGL_ERR(logger, format, args...)   clogLogger(logger, CLOGL_LEVEL_ERR,   "[ERROR] <%s %d %s> " format, __FILE__, __LINE__, __FUNCTION__, ##args);
#define CLOGL_WARN(logger, format, args...)  clogLogger(logger, CLOGL_LEVEL_WARN,  "[WARN] <%s %d %s> " format, __FILE__, __LINE__, __FUNCTION__, ##args);
#define CLOGL_INFO(logger, format, args...)  clogLogger(logger, CLOGL_LEVEL_INFO,  "[INFO] <%s %d %s> " format, __FILE__, __LINE__, __FUNCTION__, ##args);
#define CLOGL_DEBUG(logger, format, args...) clogLogger(logger, CLOGL_LEVEL_DEBUG, "[DEBUG] <%s %d %s> " format, __FILE__, __LINE__, __FUNCTION__, ##args);
#else
#define CLOGL_DATA(logger, format, args...)  clogLogger(logger, CLOGL_LEVEL_DATA,  "[DATA] " format, ##args);
#define CLOGL_ERR(logger, format, args...)   clogLogger(logger, CLOGL_LEVEL_ERR,   "[ERROR] <%d %s> " format, __LINE__, __FUNCTION__, ##args);
#define CLOGL_WARN(logger, format, args...)  clogLogger(logger, CLOGL_LEVEL_WARN,  "[WARN] <%d %s> " format, __LINE__, __FUNCTION__, ##args);
#define CLOGL_INFO(logger, format, args...)  clogLogger(logger, CLOGL_LEVEL_INFO,  "[INFO] <%d %s> " format, __LINE__, __FUNCTION__, ##args);
#define CLOGL_DEBUG(logger, format, args...) clogLogger(logger, CLOGL_LEVEL_DEBUG, "[DEBUG] <%d %s> " format, __LINE__, __FUNCTION__, ##args);
#endif


#if defined (__cplusplus)
}   
#endif /* defined (__cplusplus) */

#endif // CLOGL_H
