/*
 * C语言日志记录
 * 可以多线程, 可以日志分级, 可以设置记录级别, 可以输出到多个方向
 * WARN!!! -> 初始化过程可不是线程安全的. 信号处理的过程也不是线程安全的!!!
 *
 * 作者:
 *    刘恒(liuhengloveyou@gmail.com)
 * 时间:
 *    2011.10.20
 */


#include "clogl.h"

clogl_t *clogls; // 保存系统中所有的日志对象

/*
  clogl自身错误打印
 */
static void cloglErr(const char *msg)
{
	if (!msg) {
		fprintf(stderr, "cloglErr null!!!\n");
		return;
	}

	FILE *fp = fopen("/tmp/CLOGL.ERR", "a");
	if (NULL == fp) {
		fprintf(stderr, "cloglErr error!!! '%s'\n", msg);
		return;
	}
	
	char tb[32] ={0,};
	time_t clock = {0,};
	struct tm *tm = NULL;
	time(&clock);
	tm = localtime(&clock);
	strftime(tb, 20, "%Y-%m-%d %X", tm);
	
	fprintf(fp, "%s %s\n", tb, msg);
	fflush(fp);
	fclose(fp);
	
	return;
}

/*
  获得线程私有日志缓冲区
 */
static clogMsg *getMsgBuff(clogl_t *log)
{
	clogMsg *msg = (clogMsg *)pthread_getspecific(log->msgp);
	if (!msg) {
		msg = (clogMsg *)calloc(1, sizeof(clogMsg));
		if (!msg) {
			return NULL;
		}
		if (pthread_setspecific(log->msgp, msg)) {
			return NULL;
		}
	}

	return msg;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
  clogl基本日志格式化. 每个格式都要先经它处理
  buf: 日志信息缓存; begin:缓存开头长度;
 */
static int cloglBaseFmt(clogMsg **buff, size_t begin, const char *fmt, va_list args)
{
	clogMsg *buffp = *buff;
	if (!buffp) {
		return -1;
	}
	if ((buffp->msgSize > 0) && (begin >= buffp->msgSize)) {
		return -1;
	}
	if (NULL == buffp->msgBuff) {
		buffp->msgSize = 1024;
		buffp->msgBuff = (char *)malloc(buffp->msgSize);
		if (NULL == buffp->msgBuff) {
			return -1;
		}
		memset(buffp->msgBuff, 0, buffp->msgSize);
	}

	while (1) {		
		char *msg = buffp->msgBuff + begin;
		int len = buffp->msgSize - begin;

		va_list vl;
		va_copy(vl, args);
		int n = vsnprintf(msg, len, fmt, vl);

		if (n > CLOGL_MSG_MAX) { //* 日志信息超长
			strcpy(msg, "LOG TOO LONG");
			break;
		}
		
		if (n > -1 && n < len) {
			break; // 这样是格式化成功了
		}

		if (n >= len)
			buffp->msgSize = n + begin + 64;

		char *nf = (char *)realloc(buffp->msgBuff, buffp->msgSize);
		if (!nf) {
			cloglErr("cloglBaseFmt realloc error...");
			free(buffp->msgBuff);
			buffp->msgBuff = NULL;
			buffp->msgSize = 0;
		} else {
			buffp->msgBuff = nf;
		}
	}

	return 0;
}

/*
 *  clogl默认基本日志格式. 只加了一个时间
 */
static char *cloglDefFmt(clogl_t *log, const char *format, va_list args)
{
	// 取日志缓冲区
	clogMsg *msg = getMsgBuff(log);
	if (!msg) {
		cloglErr("cloglDefFmt getMsgBuff null");
		return NULL;
	}

	int rst = cloglBaseFmt(&msg, 20, format, args);
	if (rst) {
		return NULL;
	}

	time_t clock = {0,};
	struct tm *tm = NULL;
	time(&clock);
	tm = localtime(&clock);
	strftime(msg->msgBuff, 20, "%Y-%m-%d %X", tm);
	msg->msgBuff[19] = ' ';

	return msg->msgBuff;
}

/*
 *  clogl 默认日志格式. 加时间, 进程ID， 线程ID
 */
static char *cloglIDFmt(clogl_t *log, const char *format, va_list args)
{
	// 取日志缓冲区
	clogMsg *msg = getMsgBuff(log);
	if (!msg) {
		cloglErr("cloglIDFmt getMsgBuff null");
		return NULL;
	}

	int rst = cloglBaseFmt(&msg, 34, format, args);
	if (rst) {
		return NULL;
	}

	// 日志前面的时间
	time_t clock = {0,};
	struct tm *tm = NULL;
	time(&clock);
	tm = localtime(&clock);
	strftime(msg->msgBuff, 20, "%Y-%m-%d %X", tm);

	// pid tid
	snprintf(msg->msgBuff+19, 15, " <%5d %5d>", getpid(), (int)syscall(__NR_gettid));
	msg->msgBuff[33] = ' ';

	return msg->msgBuff;
}
/* 系统中所有日志格式 */
static cloglFmt cloglFmts[3] = {
	{(char*)"defFmt", cloglDefFmt},
	{(char*)"ptidFmt", cloglIDFmt},
	{NULL, NULL}
};

/*
 * 功能:
 *    跟据传的名字, 返回一个日志格式对象
 * 入参:
 *    name: 日志格式名
 * 出参:
 *    NO
 * 返回值:
 *    成功返回日志格式对象指针, 出错返回 NULL
 */
static cloglFmt* cloglGetFmt(const char *name)
{
	if (!name || !name[0])
		return NULL;

	cloglFmt *tmpFmt = &cloglFmts[0];

	while (tmpFmt) {
		if (!strcmp(name, tmpFmt->name)) {
			return tmpFmt;
		}
		tmpFmt ++;
	}

	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
  功能:
     打开一个输出方向
  入参:
     apd: 一个输出方向结构
 */
static inline int cloglApdOpen(cloglApd *apd)
{
    if (!apd)
	return -1;

    if (!apd->apdType)
	return -1;
       
    if (!apd->apdType->open)
	return 0;

    return apd->apdType->open(apd);
}

/*
 功能:
    关闭一个输出方向
 入参:
    apd: 一个输出方向结构
 */
static inline int cloglApdClose(cloglApd *apd)
{
    if (!apd)
	return -1;

    if (!apd->apdType)
	return -1;
       
    if (!apd->apdType->close)
	return 0;

    return apd->apdType->close(apd);
}


/* 终端输出方向 >>> */
static int term_open(cloglApd *apd)
{
	apd = apd;
	return 0;
}
static int term_close(cloglApd *apd)
{
	apd = apd;
	return 0;
}
static int term_append(cloglApd *apd, const char *msg)
{
	apd = apd;
	fprintf(stderr, "%s\n", msg);
	return 0;
}
/* 终端输出方向 <<<*/

/* 按时间产生新的日志文件. 单位小时 >>>*/
static int timeFile_open(cloglApd *apd)
{
	cloglTimeFileOpt *opt = (cloglTimeFileOpt *)apd->opt;
	if (!opt) {
		return -1;
	}
	if (!opt->fileName || !opt->fileName[0]) {
		return -1;
	}

	opt->fp = fopen(opt->fileName, "a");
	if (!opt->fp) {
		return -1;
	}

	opt->now = time(NULL); // 打开时间
	apd->isOpen = 1;

	return 0;
}
static int timeFile_close(cloglApd *apd)
{
	cloglTimeFileOpt *opt = (cloglTimeFileOpt *)apd->opt;
	if (!opt) {
		return -1;
	}

 	if (opt->fp) {
 		if (fclose(opt->fp)) {
			return -1;
		}
 		opt->fp = NULL;
		apd->isOpen = 0;
 	}

	return 0;
}

static int timeFile_append(cloglApd *apd, const char *msg)
{
	if (!msg) {
		return -1;
	}

	cloglTimeFileOpt *opt = (cloglTimeFileOpt *)apd->opt;
	if (!opt) {
		return -1;
	}

	if (!opt->fp) {
		return -1;
	}
	fprintf(opt->fp, "%s\r\n", msg);
	fflush(opt->fp); // 没有更新需求前, 保持每次flush 2012.12.20
	/*
	if (CLOGL_LEVEL_ERR >= apd->priority) {
		fflush(opt->fp);
	}
	*/
	return 0;
}
/* 按时间间隔换日志文件 */
static int timeFile_event(cloglApd *apd)
{
	if (0 == apd->isOpen) // 还没记日志
		return 0;

	cloglTimeFileOpt *opt = (cloglTimeFileOpt *)apd->opt;
	if (!opt) {
		return -1;
	}
	if (!opt->fp) {
		return -1;
	}
	if (!opt->fileName || !opt->fileName[0]) {
		return -1;
	}
	if (0 == opt->now || 0 == opt->span) {
		return -1;
	}

	time_t nowTime = time(NULL);
	if ((nowTime - opt->now) < opt->span) {
		return 0;
	}

	// 关现在日志文件
	if (cloglApdClose(apd)) {
		return -1;
	}

	// 备份文件
	int len = strlen(opt->fileName);
	char *newName = (char *)calloc(len+32, sizeof(char));
	if (!newName) {
		return -1;
	}
	(void)strcpy(newName, opt->fileName);
	newName[len] = '.';
	struct tm *t = localtime(&opt->now);
	strftime(newName+len+1, 20, "%Y-%m-%d %X", t);
	if (rename(opt->fileName, newName)) {
		cloglErr("timeFile_event rename error");
		free(newName);
		return -1;
	}
	free(newName);
	
	return 0;
}
/* 每小时换日志文件 */
static int hourFile_event(cloglApd *apd)
{
	if (0 == apd->isOpen)  // 还没记日志
		return 0;
	
	cloglTimeFileOpt *opt = (cloglTimeFileOpt *)apd->opt;
	if (!opt) {
		return -1;
	}
	if (!opt->fp) {
		return -1;
	}
	if (!opt->fileName || !opt->fileName[0]) {
		return -1;
	}

	if (0 == opt->now)
		return 0;

	// 测试时间
	time_t tNow = time(NULL);
	time_t tLast = opt->now;
	struct tm *nowTime = localtime((const time_t*)&tNow);
	int hour = nowTime->tm_hour;
	nowTime = localtime((const time_t*)&tLast);
	if (hour == nowTime->tm_hour) {
		return 0;
	}

	// 关日志文件
	if (cloglApdClose(apd)) {
		return -1;
	}

	// 备件文件
	int len = strlen(opt->fileName);
	char *newName = (char *)calloc(len+32, sizeof(char));
	if (!newName) {
		return -1;
	}
	(void)strcpy(newName, opt->fileName);
	newName[len] = '.';
	strftime(newName+len+1, 16, "%Y-%m-%d-%H", localtime((const time_t*)&opt->now)); //%M%S
	if (rename(opt->fileName, newName)) {
		char buff[128] = {0};
		sprintf(buff, "hourFile_event rename error: '%d', '%s', '%s'", errno, opt->fileName, newName);
		cloglErr((const char *)buff);
		free(newName);
		return -1;
	}
	free(newName);
	
	return 0;
}
/* 按时间产生新的日志文件. 单位小时 <<<*/

/* 按文件大小产生新的文件. 单位兆 >>>
static int sizeFile_open(cloglApd *apd)
{
	cloglRollFileOpt *opt = (cloglRollFileOpt *)apd->opt;
	if (!opt || 0 == opt->fileName[0])
		return -1;

	pthread_mutex_lock(&apd->pLock);
	if (!apd->isOpen && !opt->fp) {
		apd->isOpen ++;
		opt->fp = fopen(opt->fileName, "a");
	}
	pthread_mutex_unlock(&apd->pLock);

	if (!opt->fp)
		return -1;

	return 0;
}
static inline int sizeFile_close(cloglApd *apd)
{

	cloglRollFileOpt *opt = apd->opt;
 	if (!opt)
 		return 0;

	pthread_mutex_lock(&apd->pLock);
 	if (opt->fp) {
		fclose(opt->fp);
		opt->fp = NULL;
		apd->isOpen = 0;
	}
	pthread_mutex_unlock(&apd->pLock);

	return 0;
}
static int sizeFile_append(cloglApd *apd, const char *msg)
{
	cloglRollFileOpt *opt = apd->opt;
 	if (!opt || !opt->fp)
 		return -1;

	pthread_mutex_lock(&apd->pLock);
	if (opt->fp) {
		fprintf(opt->fp, "%s\n", msg);
		fflush(opt->fp);
	}
	pthread_mutex_unlock(&apd->pLock);

	return 0;

}
static int sizeFile_event(cloglApd *apd)
{
	pthread_mutex_lock(&apd->pLock);
	cloglRollFileOpt *opt = (cloglRollFileOpt*)apd->opt;

	// 判断存在
	if (!access(opt->fileName, F_OK)) {
		// 如果存在, 是否是原文件？
		// @@
	} else {
		// 如果不存在, 重新打开
		if (apd->isOpen) {
			fclose(opt->fp);
			opt->fp = NULL;
			apd->isOpen = 0;
		}
		opt->fp = fopen(opt->fileName, "a");
		apd->isOpen ++;
		pthread_mutex_unlock(&apd->pLock);
		return;
	}


	// 判断大小
	struct stat buff;
	int rst = stat(opt->fileName, &buff);
	if (-1 == rst)
		return;

	if (buff.st_size >= opt->maxSize*1024*1024) {
		// 如果超大了， 重新打开新文件
		if (apd->isOpen) {
			fclose(opt->fp);
			opt->fp = NULL;
			apd->isOpen = 0;
		}

		char cmd[600] = {0,};
		sprintf(cmd, "%s%s%s%s%s%s%s%s%s", "mv -f ", opt->fileName, ".b ", opt->fileName, ".c &>/dev/null && mv -f ", opt->fileName, "  ", opt->fileName, ".b &>/dev/null;");
		if (-1 == system(cmd))
			fprintf(stderr, "roolFileEvent error!!!\n\n");

		opt->fp = fopen(opt->fileName, "a");
		apd->isOpen ++;

	}
	pthread_mutex_unlock(&apd->pLock);

	return 0;
}
//cloglApdT clogl_apdType_sizeFile = {(char *)"SizeFile", sizeFile_open, sizeFile_append, sizeFile_close, sizeFile_event};
按文件大小产生新的文件. 单位兆 <<< */

static cloglApdT cloglApdTypes[4] = {
	{(char *)"Console", term_open, term_append, term_close, NULL},
	{(char *)"TimeFile", timeFile_open, timeFile_append, timeFile_close, timeFile_event},
	{(char *)"HourFile", timeFile_open, timeFile_append, timeFile_close, hourFile_event},
	{NULL , NULL, NULL, NULL, NULL}
};

static cloglApdT* cloglGetApd(const char *name)
{
	if (!name || !name[0])
		return NULL;

	for (cloglApdT *tmpapdt = &cloglApdTypes[0]; tmpapdt->name; tmpapdt++) {
		if (!strcmp(name, tmpapdt->name)) {
			return tmpapdt;
		}
	}

	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
  启动一个线程， 定时查看得日志输出方向的状态
 */
static void *threadEvert(void *parm)
{
	parm = parm;
	
	useconds_t sleepTime = CLOGL_EVENT_TIME * 1000;

	sleep(5); // 等一下业务线程

	while (1) {
		(void)usleep(sleepTime);
		for (clogl_t *tmp = clogls; tmp; tmp = tmp->next) {
			for (cloglApd *tmpApd = tmp->apds; tmpApd; tmpApd = tmpApd->next) {
				cloglApdT *tmpApt = tmpApd->apdType;
				if (tmpApt && tmpApt->event) {
					pthread_mutex_lock(&tmpApd->pLock);
					(void)tmpApt->event(tmpApd);
					pthread_mutex_unlock(&tmpApd->pLock);
				}
			}
		}
	}

	return (void *)0;
}

/*
 * 功能:
 *    向一个输出方向输出日志
 * 入参:
 *    apd:      一个输出方向结构
 *    priority: 日志级别
 *    logBuff:  存放格式化后日志信息的BUFF
 *    format:   日志信息
 * 出参:
 *    logBuff:  存放格式化后日志信息的BUFF
 * 返回值:
 *    OK 0, error -1
 */
static int cloglApdAppend(cloglApd *apd, int priority, char *logBuff)
{
	if (!apd->apdType)
		return -1;

	if (!apd->apdType->append)
		return -1;

	if (apd->priority < priority)
		return 0;

	pthread_mutex_lock(&apd->pLock);
	if (!apd->isOpen) { 
		if (cloglApdOpen(apd)) {
			pthread_mutex_unlock(&apd->pLock);
			return -1;
		}
	}	
	int rst = apd->apdType->append(apd, logBuff);
	pthread_mutex_unlock(&apd->pLock);

	return rst;
}

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
void clogLogger(clogl_t *log, int priority, const char *format, ...)
{	
	if (!log)
		return;

	if (!log->apds)
		return;

	if (log->priority < priority)
		return;

	// 格式化日志信息. 最长512K
	char *logMsg = NULL;

	// 发送到多个输出方向
	for (cloglApd *tmpapd = log->apds; tmpapd; tmpapd = tmpapd->next) {	
		if (!tmpapd->fmt)
			continue;
		if (!tmpapd->fmt->format)
			continue;

		logMsg = NULL;

		va_list va;
		va_start(va, format);
		if (CLOGL_LEVEL_DATA == priority) {
			logMsg = cloglDefFmt(log, format, va);  /* DATA级别的日志特别处理 !!! */
		} else {
			logMsg = tmpapd->fmt->format(log, format, va);
		}
		va_end(va);

		(void)cloglApdAppend(tmpapd, priority, logMsg); // 输出日志
	}
}


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
int cloglInit()
{
	// 启动事件线程
	pthread_t ptid = 0;
	int rst = pthread_create(&ptid, NULL, threadEvert, NULL);
	if (rst) {
		return -1;
	}
	// 使线程处于分离状态
	rst = pthread_detach(ptid);
	if (rst) {
		return -1;
	}

	return 0;
}

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
int setLogPriority(clogl_t *log, int p)
{
	if (p < CLOGL_LEVEL_ERR || p >= CLOGL_LEVEL_UNKNOWN)
		return -1;
	if(!log)
		return -1;

	log->priority = p;

	for (cloglApd *tmpapd = log->apds; tmpapd; tmpapd = tmpapd->next) {
		tmpapd->priority = p;
	}

	return 0;
}

/*
  释放日志缓冲区资源
 */
void freeMsgBuff(void *msgp)
{
	clogMsg *msg = (clogMsg *)msgp;
	(void)free(msg->msgBuff);
	(void)free(msg);
	msg = NULL;
}

/*
 * 功能:
 *    获得一个按时间产生新的日志文件的默认日志对象指针
 * 入参:
 *    日志对象名
 * 出参:
 *    NO
 * 返回值:
 *    成功返回日志对象指针, 出错返回 NULL
 */
clogl_t *cloglGetDftTimeFile(const char *name)
{
	static int only; // 系统中只能有一个相同的日志对象存在
	if (only)
		return NULL;

	if (!name || !name[0])
		return NULL;

	clogl_t *tmpLog = (clogl_t*)calloc(1, sizeof(clogl_t));
	if (!tmpLog)
		return NULL;

	// 日志对象名
	tmpLog->name = (char*)calloc(strlen(name)+1, sizeof(char));
	if (!tmpLog->name) {
		free(tmpLog);
		return NULL;
	}
	(void)strcpy(tmpLog->name, name);

	// 输出级别
	tmpLog->priority =  CLOGL_LEVEL_DEBUG;

	// 日志缓冲区
	int rst = pthread_key_create(&tmpLog->msgp, freeMsgBuff);
	if (rst){
		free(tmpLog->name);
		free(tmpLog);
		return NULL;
	}

	/* 输出方向 */
	cloglApd *tmpApd = (cloglApd *)calloc(1, sizeof(cloglApd));
	if (!tmpApd) {
		free(tmpLog->name);
		free(tmpLog);
		return NULL;
	}
	// 输出方向名
	tmpApd->name = (char *)calloc(16, sizeof(char));
	if (!tmpApd->name) {
		free(tmpLog->name);
		free(tmpLog);
		free(tmpApd);
		return NULL;
	}
	(void)strcpy(tmpApd->name, "dftTimeFileApd");
	// 输出方输出级别
	tmpApd->priority = CLOGL_LEVEL_DEBUG;
	// 未打开状态
	tmpApd->isOpen = 0;
	// 输出方向类型
	tmpApd->apdType = cloglGetApd("HourFile");
	if (!tmpApd->apdType) {
		free(tmpLog->name);
		free(tmpLog);
		free(tmpApd);
		return NULL;
	}
	// 格式
	tmpApd->fmt = cloglGetFmt("ptidFmt");
	// 初始化线程锁
	pthread_mutex_init(&tmpApd->pLock, NULL);

	/* 属性 */
	cloglTimeFileOpt *tmpOpt = (cloglTimeFileOpt *)calloc(1, sizeof(cloglTimeFileOpt));
	if (!tmpOpt) {
		free(tmpLog->name);
		free(tmpLog);
		free(tmpApd->name);
		free(tmpApd);
		return NULL;
	}
	// 日志文件名. 默认当前进程可执行文件所在目录下的logs目录, 文件名与可执行文件名相同
	pid_t pid = getpid();
	char procf[32] = {0,};
	char exe[2048] = {0,};
	(void)snprintf(procf, sizeof(procf), "%s%d%s", "/proc/", pid, "/exe");
	ssize_t err = readlink(procf, exe, sizeof(exe));
	if (-1 == err) {
		free(tmpLog->name);
		free(tmpLog);
		free(tmpApd->name);
		free(tmpApd);
		free(tmpOpt);
		return NULL;
	}
	char *exep = strrchr(exe, '/');
	if (!exep) {
		free(tmpLog->name);
		free(tmpLog);
		free(tmpApd->name);
		free(tmpApd);
		free(tmpOpt);
		return NULL;
	}
	char execname[256] = {0}; // 可执行文件名
	strncpy(execname, exep+1, sizeof(execname));

	// 建logs目录
	strcpy(exep+1, "logs");
	if (!access(exe, F_OK)) {
		// 如果存在, 是不是目录?
		struct stat filestat;
		if (stat(exe, &filestat)) {
			free(tmpLog->name);
			free(tmpLog);
			free(tmpApd->name);
			free(tmpApd);
			free(tmpOpt);
			return NULL;
		}
		if (!S_ISDIR(filestat.st_mode))	{
			if (-1 == mkdir(exe, 0755)) {
				free(tmpLog->name);
				free(tmpLog);
				free(tmpApd->name);
				free(tmpApd);
				free(tmpOpt);
				return NULL;
			}
		}
	} else {
		// 如果不存在, mkdir
		if (-1 == mkdir(exe, 0755)) {
			free(tmpLog->name);
			free(tmpLog);
			free(tmpApd->name);
			free(tmpApd);
			free(tmpOpt);
			return NULL;
		}
	}

	// 日志文件名
	int len = strlen(exe) + strlen(execname) + 64;
	tmpOpt->fileName = (char *)calloc(len, sizeof(char));
	if (!tmpOpt->fileName) {

	}
	snprintf(tmpOpt->fileName, len, "%s/%s%s", exe, execname, ".log");

	// 默认简隔1小时
	tmpOpt->span = 1 * 60 * 60;

	// 赋值属性
	tmpApd->opt = tmpOpt;

	tmpLog->apds = tmpApd;

	// 加到系统日志对象链中
	if (!clogls) {
		clogls = tmpLog;
	} else {
		clogl_t **tmp = &clogls->next;
		while (*tmp)
			tmp = &((*tmp)->next);

		*tmp = tmpLog;
	}

	only ++;

	return tmpLog;
}

/*
 * 功能:
 *    获得一个日志对象指针
 * 入参:
 *    日志对象名
 * 出参:
 *    NO
 * 返回值:
 *    成功返回日志对象指针, 出错返回 NULL
 */
clogl_t *cloglGet(const char *name)
{
	if (!name || !name[0]) {
		return NULL;
	}

	clogl_t *tmplog = clogls;
	while (tmplog) {
		if (!strcmp(tmplog->name, name))
			return tmplog;
		tmplog = tmplog->next;
	}

	return NULL;
}

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
clogl_t *cloglGetDft()
{
#define DEFAULT_LOG_NAME "noname"  // 系统中默认日志对象名

	clogl_t *tmpLog = cloglGet(DEFAULT_LOG_NAME);

	if (!tmpLog)
		tmpLog = cloglGetDftTimeFile(DEFAULT_LOG_NAME);

	return tmpLog;
}

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
clogl_level cloglLevel(const char *lvl)
{
	if (!lvl || !lvl[0]) {
		return CLOGL_LEVEL_UNKNOWN;
	}
		
	if(0 == strcmp(lvl, "DATA")) {
		return CLOGL_LEVEL_DATA;
	} else if(0 == strcmp(lvl, "ERROR")) {
		return CLOGL_LEVEL_ERR;
	} else if(0 == strcmp(lvl, "WARN")) {
		return CLOGL_LEVEL_WARN;
	} else if(0 == strcmp(lvl, "INFO")) {
		return CLOGL_LEVEL_INFO;
	} else if(0 == strcmp(lvl, "DEBUG")) {
		return CLOGL_LEVEL_DEBUG;
	}

	return CLOGL_LEVEL_UNKNOWN;
}

#if 0
#include <sys/time.h>
static void *threadTest(void *args)
{
	clogl_t *mylog = cloglGet("noname");
	if (!mylog) {
		fprintf(stderr, "get log error\n");
		return NULL;
	}
	setLogPriority(mylog, CLOGL_LEVEL_DEBUG);

	struct timeval tv1 = {0,0};
	gettimeofday(&tv1, NULL);

	for (int i = 0; i < 100000; i++) {
		CLOGL_DEBUG(mylog, "这是一条测试日志aaa %s %ld abc", "OO", pthread_self());
		CLOGL_ERR(mylog, "这是一条测试日志aaa %s %ld abc", "OO", pthread_self());
		CLOGL_DEBUG(mylog, "这是一条测试日志aaa %s %x", "OO", pthread_self());
		CLOGL_ERR(mylog, "这是一条测试日志aaa %s %x", "OO", pthread_self());
		
		// cloglErr("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		
	}
	struct timeval tv2 = {0,0};
	gettimeofday(&tv2, NULL);

	fprintf(stderr, ">>%ld %ld\n", tv2.tv_sec-tv1.tv_sec, tv2.tv_usec-tv1.tv_usec);

	return (void *)0;

}

// gcc -std=gnu99 -O2 clogl.c -lpthread -o clogl
int main(int argc, char *argv[])
{

	int err = cloglInit();
	if (err) {
		fprintf(stderr, "pthread init error");
		return -1;
	}

	clogl_t *mylog = cloglGetDft();
	if (!mylog) {
		fprintf(stderr, "get appLog error\n");
		return -1;
	}

	// 启动测试线程
	int i = 0;
	for (i = 0; i < 1000; i++) {
		pthread_t ptid = 0;
		int rst = pthread_create(&ptid, NULL, threadTest, NULL);
		if (rst) {
			fprintf(stderr, "pthread create error");
			return -1;
		}
		// 使线程处于分离状态
		rst = pthread_detach(ptid);
		if (rst) {
			fprintf(stderr, "pthread detach error");
			return -1;
		}
		fprintf(stderr, "thread %ld\n", ptid);
	}

	sleep(1000);


	return 0;
}
#endif
