/**
* @file tcp.h
* @brief Интерфейс tcp посредством библиотки winsock2.h.
* @todo Если останется время, сделать поддержку кросплатформенности для Linux.
* @todo Подумать насчет порядка байт Little Endian - Big Endian, так как 
* на разных машинах он может оказаться разным и по-разному передаваться в TCP.
*/
#pragma once

#if defined (WIN32) || (_WIN64)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
static inline int poll(pollfd* pfd, int nfds, int timeout) { return WSAPoll(pfd, nfds, timeout); }
#else
/**
* @todo Устранить возможную некорректную работу, если некоторые функции будут возвращать
* в качестве ошибки значение меньше 0, но не (-1).
* В win SOCKET определен как unsigned, соответственно SOCKET_ERROR и INVALID_SOCKET в 
* зависимости от разрядности принимают значение 0xFFFFFFFF.... и не могут быть 
* отрицательными.
*/
#define SOCKET int
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET SOCKET_ERROR
#endif

#include "out.h"

class Tcp
{
	/**
	* @note Предварительно необходимо настроить pgAdmin на порт 5433 и отключить SSL.
	*/
	static const int clntPort = 5433; // Клиент для прокси - pgAdmin.
	static const int servPort = 5432; // Сервер для прокси - сервер PostgreSQL.
	const char* ip; ///@todo Протестировать для других IP, кроме 127.0.0.1.

	Out* out;
	char* buf;
	/**
	* Структуры для мониторинга необходимости записи в файл.
	*/
	Out::Need* sNeed;

	const int maxConn, buflen;
	int i; ///@note Не использовать во вложенных циклах.

	SOCKET sockToServ, sockToClnt, sock;
	/**
	* Структуры для мониторинга событий:
	* 0 - входящие соединения от клиентов;
	* 1, 3, 5 ... (2 * maxConn - 1) - события на сокетах клиентов;
	* 2, 4, 6 ... (2 * maxConn) - события на сокетах сервера;
	*/
	pollfd *sPoll;

	sockaddr_in saToClnt, saToServ; ///@todo Подумать о начальной инициализации (обнуление).
	
	int maxInd, bytesNo;

	int init_win(void);
	int deinit_win(void);

public:
	int init(void) { return init_win(); }
	int deinit(void) { return deinit_win(); }
	Out::Ans polling();
	Tcp(int maxConn_, int buflen_, char* buf_);
	~Tcp();
};

