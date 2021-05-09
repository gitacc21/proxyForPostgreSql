/**
* @file tcp.cpp
* @brief См. описание в заголовочном файле.
*/
#include "tcp.h"

Tcp::Tcp(int maxConn_, int buflen_, char* buf_):
    out(NULL), buf(buf_), sNeed(NULL), maxConn(maxConn_), buflen(buflen_),
    sockToServ(INVALID_SOCKET), sockToClnt(INVALID_SOCKET), sock(INVALID_SOCKET),
    sPoll(NULL), maxInd(0), bytesNo(0)
{
    /**
    * @todo: Подумать насчет поддержки IPv6.
    */
    const char* dfltIp = "127.0.0.1";
    ip = dfltIp;
}

Tcp::~Tcp()
{
    delete out;
    out = NULL;
    delete[] sPoll;
    sPoll = NULL;
    delete[] sNeed;
    sNeed = NULL;
}

int Tcp::init_win(void)
{
    int res = 0;
    out = new Out(res);
    if (res != 0 || out == NULL) return res - 1000;
    if ((sPoll = new pollfd[2 * maxConn + 1]) == NULL) return -1000;
    for (int i = 0; i < maxConn * 2 + 1; i++)
    {
        sPoll[i].fd = INVALID_SOCKET; // Дескриптор сокета не установлен, игнорировать.
        sPoll[i].events = POLLIN; ///@todo Разобрать ситуации POLLRDNORM, POLLRDBAND.
    }
    if ((sNeed = new Out::Need[maxConn + 1]) == NULL) return -999;
    for (int i = 0; i < maxConn + 1; i++)
    {
        sNeed[i].write = false;
    }

    WSADATA wsa_data; // Для Win необходимо инициализировать библиотеку.
    if (WSAStartup(0x101, &wsa_data) || wsa_data.wVersion != 0x101) return -1;
    
    sockToClnt = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockToClnt == INVALID_SOCKET) return -2;

	saToClnt.sin_family = AF_INET;
	saToClnt.sin_port = htons(clntPort);
	saToClnt.sin_addr.S_un.S_addr = inet_addr(ip); ///@todo inet_addr ошибка при ip=255.255.255.255
    if (bind(sockToClnt, (sockaddr*)&saToClnt, sizeof(saToClnt)) == SOCKET_ERROR) return -3;

    saToServ.sin_family = AF_INET;
    saToServ.sin_port = htons(servPort);
    saToServ.sin_addr.S_un.S_addr = inet_addr(ip); ///@todo inet_addr ошибка при ip=255.255.255.255

    /**
    * @note maxConn в данном случае - максимальное число established соединений, 
    * ожидающих accept.
    */
    if (listen(sockToClnt, maxConn) != 0) return -7;

    /**
    * Установим событие при поступлении входящего соединения к прокси.
    */
    sPoll[0].fd = sockToClnt;
    sPoll[0].events = POLLIN; ///@todo Разобрать ситуации POLLRDNORM, POLLRDBAND.

    return 0;
}
/**
* @todo сделать обработку ошибок
*/
int Tcp::deinit_win(void)
{
    closesocket(sockToServ);
    closesocket(sockToClnt);
    WSACleanup();
    return 0;
}

Out::Ans Tcp::polling()
{
    /**
    * @note Используем i (переменную класса) без необходимости
    * предварительного объявления только если цикл не вложенный.
    */

    /**
    * Проверяем, есть ли необходимость записать в файл.
    */
    for (i = 1; i < maxInd + 1; i++)
    {
        if (sNeed[i].write)
        {
            out->write_string(sNeed[i]);
            sNeed[i].write = false;
        }
    }
    /**
    * Бесконечно ожидаем событий.
    */
    int ret = poll(sPoll, maxInd * 2 + 1, (-1)); 
    if (ret == -1) return Out::Ans::ERR;

    /**
    * Событие: установка соединения с прокси.
    * @todo Рассмотреть возможность возникновения событий sPoll[0] с ошибками:
    * POLLERR, POLLHUP etc.
    */
    if (sPoll[0].revents & POLLIN)
    {
        sPoll[0].revents &= ~POLLIN; // Обнулим флаги.
        for (i = 1; i < maxConn + 1; i++) //Найдем пустой слот для соединения клиент-сервер.
        {
            /**
            * Найдем слот и создадим 2 соединения: с клиентом и с сервером.
            * Индекс i создает взаимно однозначное соответствие:
            * нечетный - к клиенту, далее за ним четный - к серверу.
            * @todo Доработать код, исключив не совсем понятную конструкцию i * 2 - 1
            */
            if (sPoll[i * 2 - 1].fd == INVALID_SOCKET) 
            {
                sock = accept(sockToClnt, NULL, NULL); // От клиента.
                if (sock == SOCKET_ERROR) return Out::Ans::ERR1;
                sPoll[i * 2 - 1].fd = sock;
                sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // К серверу.
                /**
                * @todo Подумать над тем, чтобы коннект не блокировал работу
                * прокси на время установления соединения с сервером (возможно, сделать
                * неблокируемый сокет).
                */
                if (sock == INVALID_SOCKET || 
                    connect(sock, (sockaddr*)&saToServ, sizeof(saToServ)) == SOCKET_ERROR)
                {
                    closesocket(sPoll[i].fd);
                    return Out::Ans::ERR1;
                }
                sPoll[i * 2].fd = sock;
                if (i > maxInd) maxInd = i; // Максимальный текущий номер соединения.
                break;
            }
            if (i == maxConn) return Out::Ans::CLNT_OVERLOAD; // Превышено макс кол-во клиентов.
        }
        if (--ret <= 0) return Out::Ans::CONTINUE; // Больше нет событий.
    }
    /**
    * Событие: есть входящие данные (от сервера или клиентов)
    * или в буфере TCP есть место не менее текущего значения минимального количества 
    * байт для буфера отправки.
    * @todo Настроить текущее значение минимального количества 
    * байт для буфера отправки под нужды прокси (не менее чем buflen).
    */
    for (i = 1; i < maxInd * 2 + 1; i++)
    {
        if ((sock = sPoll[i].fd) == INVALID_SOCKET) continue;
        if (sPoll[i].revents & (POLLIN | POLLERR))
        {
            sPoll[i].revents &= ~(POLLIN | POLLERR);
            if ((bytesNo = recv(sock, buf + i * buflen, buflen, 0)) <= 0)
            {
                /**
                * В данном случае, соединение завершено либо по инициативе собеседника
                * или оно разорвано. Необходимо закрыть сокеты к клиенту и к серверу.
                */
                closesocket(sock);
                sPoll[i].fd = INVALID_SOCKET;
                if (i & 1) // Проверим на четность, чтобы определить клиент или сервер.
                {
                    closesocket(sPoll[i + 1].fd);
                    sPoll[i + 1].fd = INVALID_SOCKET;
                }
                else
                {
                    closesocket(sPoll[i - 1].fd);
                    sPoll[i - 1].fd = INVALID_SOCKET;
                }
            }
            else
            {
                /**
                * Проверим на четность, от кого получена информация с TCP:
                * чет - клиент, нечет - сервер.
                * Далее определим, на какой сокет необходимо переправить сообщение.
                * В конце добавим событие: как только буфер TCP готов для отправки, 
                * отправить сообщение.
                * @note Подразумевается, что для конкретного подключенного клиента
                * происходит цикл: клиент->прокси->сервер->прокси->клиент. Если цикл 
                * нарушается, то необходимо использовать вместо sNeed.bytes другое
                * место для хранения отправляемого с прокси количества байт.
                */
                if (i & 1)
                {
                    sNeed[(i + 1) / 2].bytes = bytesNo;
                    sPoll[i + 1].events |= POLLOUT; 
                }
                else
                {
                    sNeed[(i + 1) / 2].bytes = bytesNo;
                    sPoll[i - 1].events |= POLLOUT;
                }
            }
            if (--ret <= 0) // Если больше нет событий.
            {
                /*
                * Если крайние слоты освободились, уменьшим maxInd.
                */
                for (i = maxInd * 2 - 1; i > 0; i -= 2)
                    if (sPoll[i].fd == INVALID_SOCKET) maxInd--;
                return Out::Ans::CONTINUE;
            }
        }
        if (sPoll[i].revents & POLLOUT)
        {
            sPoll[i].revents &= ~POLLOUT;
            sPoll[i].events &= ~POLLOUT;
            /**
            * Проверим на четность, от кого поступила команда на отправку TCP
            * нечет - от клиента, чет - от сервера.
            * Затем сделаем отметку о необходимости записи в файл.
            * @note После записи в файл, соответствующий buf будет изменен: добавляется
            * терминатор \0 и убирается заголовок и байты, определяющие payload posgesql.
            * @note Подразумевается, что для конкретного подключенного клиента
            * происходит цикл: клиент->прокси->сервер->прокси->клиент. Если цикл 
            * нарушается, то необходимо использовать вместо sNeed.bytes другое
            * место для хранения отправляемого с прокси количества байт.
            */
            if (i & 1)
            {
                bytesNo = send(
                    sock,
                    buf + (i + 1) * buflen,
                    sNeed[(i + 1) / 2].bytes,
                    0);
            }
            else
            {
                bytesNo = send(
                    sock,
                    buf + (i - 1) * buflen,
                    sNeed[(i + 1) / 2].bytes,
                    0);
                sNeed[(i + 1) / 2].write = true;
                sNeed[(i + 1) / 2].ans = Out::Ans::CLNT_REQ;
                sNeed[(i + 1) / 2].buf = buf + ((i + 1) / 2) * buflen;
            }
            if (bytesNo <= 0)
            {
                /**
                * В данном случае, соединение завершено либо по инициативе собеседника
                * или оно разорвано. Необходимо закрыть сокеты к клиенту и к серверу.
                */
                closesocket(sock);
                sPoll[i].fd = INVALID_SOCKET;
                if (i & 1) // Проверим на четность, чтобы определить клиент или сервер.
                {
                    closesocket(sPoll[i + 1].fd);
                    sPoll[i + 1].fd = INVALID_SOCKET;
                }
                else
                {
                    closesocket(sPoll[i - 1].fd);
                    sPoll[i - 1].fd = INVALID_SOCKET;
                }
            }

            if (--ret <= 0) // Если больше нет событий.
            {
                /*
                * Если крайние слоты освободились, уменьшим maxInd.
                */
                for (i = maxInd * 2 - 1; i > 0; i -= 2)
                    if (sPoll[i].fd == INVALID_SOCKET) maxInd--;
                return Out::Ans::CONTINUE;
            }

        }
    }
}



