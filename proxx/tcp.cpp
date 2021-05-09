/**
* @file tcp.cpp
* @brief См. описание в заголовочном файле.
*/
#include "tcp.h"

Tcp::Tcp():
    out(NULL),
    sockToServ(INVALID_SOCKET), sockToClnt(INVALID_SOCKET), sock(INVALID_SOCKET),
    maxInd(0), bytesNo(0)
{
    /**
    * @todo: Подумать насчет поддержки IPv6.
    */
    const char* dfltIp = "127.0.0.1";
    ip = dfltIp;
    for (int i = 0; i < maxConn * 2 + 1; i++)
    {
        sPoll[i].fd = INVALID_SOCKET; // Дескриптор сокета не установлен, игнорировать.
        sPoll[i].events = POLLIN; ///@todo Разобрать ситуации POLLRDNORM, POLLRDBAND.
    }
}

Tcp::~Tcp()
{
    delete out;
    out = NULL;
}

int Tcp::init_win(void)
{
    int res = 0;
    out = new Out(res);
    if (res != 0 || out == NULL) return res - 1000;

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

Out::Ans Tcp::polling(char* buf, const int buflen)
{
    /**
    * Бесконечно ожидаем соединения.
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
        for (int i = 1; i < maxConn + 1; i++) //Найдем пустой слот для соединения клиент-сервер.
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
    * Событие: есть данные (от сервера или клиентов).
    */
    for (int i = 1; i < maxInd * 2 + 1; i++)
    {
        if ((sock = sPoll[i].fd) == INVALID_SOCKET) continue;
        if (sPoll[i].revents & (POLLIN | POLLERR))
        {
            sPoll[i].revents &= ~(POLLIN | POLLERR);
            if ((bytesNo = recv(sock, buf, buflen, 0)) <= 0)
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
                Out::Ans ans;
                /**
                * Проверим на четность, от кого пришел запрос:
                * чет - клиент, нечет - сервер.
                * Далее определим, на какой сокет необходимо переправить сообщение.
                */
                if (i & 1)
                {
                    ans = Out::Ans::CLNT_REQ;
                    sock = sPoll[i + 1].fd;
                }
                else
                {
                    ans = Out::Ans::SERV_ANS;
                    sock = sPoll[i - 1].fd;
                }
                /**
                * @todo Сделать проверку наличия места в буфере для передачи в TCP,
                * возможно, при помощи POLLOUT.
                */
                send(sock, buf, bytesNo, 0);
                /**
                * @todo Подумать насчет производительности при записи данных
                * именно здесь.
                */
                out->write(buf, bytesNo, ans);
            }
            if (--ret <= 0) // Если больше нет событий.
            {
                /*
                * Если крайние слоты освободились, уменьшим maxInd.
                */
                for (int i = maxInd * 2 - 1; i > 0; i -= 2)
                    if (sPoll[i].fd == INVALID_SOCKET) maxInd--;
                return Out::Ans::CONTINUE;
            }
        }
    }
}


