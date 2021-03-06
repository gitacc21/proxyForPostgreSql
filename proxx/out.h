/**
* @file out.h
* @brief Вывод информации, сообщений на консоль и в файл.
* @details Принимает буфер от TCP, а также, информацию о том, откуда пришла информация
* от сервера или клиента. Первоначально планировалось сделать логирование всей информации,
* проходящей через прокси-сервер. Однако в последствии, было решено остановиться
* лишь на запросах SQL от клиента с кодом запроса 'Q'.
* @todo Предусмотреть расширение функционала для возможность логирования всей информации,
* проходящей через прокси, если такой функционал потребуется в дальнейшем.
*/
#pragma once

#include <iostream>
#include <fstream>
using namespace std;

class Out
{
	ofstream* fout;
	unsigned row;
	/**
	* @note По историческим причинам первый запрос от клиента PostrgreSQL
	* приходит без кода запроса. Необходимо еще раз подумать насчет обработки
	* этого запроса.
	*/
	bool isClntStartupMessage;
	/**
	* @brief Получает буфер, прочитанный из TCP; парсит информацию и записывает обратно 
	* в этот же буфер в виде нуль-термированной строки.
	* @note Не использует стандартную библиотеку для обработки строк.
	* @todo Если стандартную библиотеку применить нельзя, то надо подумать насчет 
	* производительности: какие ньюансы использует стандартная библиотека для улучшения
	* производительности (например: копирование 4-х или 8-байтными словами за 1 машинный
	* цикл и т. д.).
	*/
	void parse(char* buf, const int bytesread);
	/**
	* @brief Получает указатель на начало данных с длиной запроса PostgreSQL и 
	* возвращает это значение в виде unsined.
	* @todo Устранить возможную ошибку, когда порядок байт MSB LSB 
	* отличается на разных машинах.
	*/
	unsigned len(const char* p);
public:
	enum class Ans
	{
		TIMEOUT1 = 0,
		TIMEOUT2,
		CLNT_REQ, // Пришел запрос от клиента.
		SERV_ANS, // Пришел ответ от сервера.
		BUSY,
		CLNT_OVERLOAD, // Превышено макс. число соединений к прокси.
		CONTINUE, // Перейти к следующей итерации.

		ERR = -100,
		ERR1,
		ERR2,
		ERR3,
	};
	/**
	* @todo Сделать проверку не превышает ли bytesread размер buf.
	*/
	struct Need
	{
		bool write;
		char* buf;
		int bytes;
		Ans ans;
	};
	void write_string(Need need);
	void write(char* buf, const int bytesread, const Ans ans);
	Out(int& res);
	~Out();
};

