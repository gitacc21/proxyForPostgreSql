/**
* @file out.cpp
* @brief См. описание в заголовочном файле.
*/
#include "out.h"

void Out::parse(char* buf, const int bytesread)
{
    unsigned lenght = 0;
    int indx = 0, wrIndex = 0;
    if (isClntStartupMessage)
    {
        lenght = len((const char*)&buf[0]);
        if (lenght < bytesread) return;
        for (int i = 4 + 4 - 1; i < lenght; i++)
        {
            if (buf[i] == 0) buf[i] = ' ';
            buf[i - (4 + 4 - 1)] = buf[i];
        }
        buf[lenght - (4 + 4 - 1)] = 0;
        return;
    }

    if (buf[0] == 'Q')
    {
        lenght = len((const char*)&buf[1]);
        if (lenght != bytesread - 1) return;
        for (int i = 5; i < bytesread; i++) buf[i - 5] = buf[i];
    }
}

unsigned Out::len(const char* p)
{
    unsigned ret = 0;
    for (int i = 0; i < 4; i++)
        ((unsigned char*)&ret)[sizeof(ret) - 1 - i] = ((unsigned char*)p)[i];
    return ret;
}

void Out::write(char* buf, const int bytesread, const Ans ans)
{
    switch (ans)
    {
    case Ans::ERR: cout << "TCP ERR" << endl; break;
    case Ans::ERR1: cout << "TCP ERR1" << endl; break;
    case Ans::ERR2: cout << "TCP ERR2" << endl; break;
    case Ans::ERR3: cout << "TCP ERR3" << endl; break;
    case Ans::TIMEOUT2: cout << "TCP TIMEOUT2" << endl; break;
    //case Tcp::Ans::BUSY: cout << "TCP BUSY" << endl; break;
    case Ans::CLNT_REQ:
        parse(buf, bytesread);
        cout << row << " CLNT_REQ: " << buf << endl;
        *fout << row << " CLNT_REQ: " << buf << endl;
        row++;
        if (isClntStartupMessage) isClntStartupMessage = false;
        break;
    case Ans::SERV_ANS:
        //parse(buf, bytesread);
        //cout << row << " SERV_ANS: " << buf << endl;
        //*fout << row << " SERV_ANS: " << buf << endl;
        //row++;
        break;
    default: break;
    }
}

Out::Out(int& res): fout(NULL), row(0), isClntStartupMessage(true)
{
    fout = new ofstream("proxy_log.txt");
    if (fout == NULL)
    {
        cout << "Can't create proxy log file!" << endl;
        res = -1;
    }
    else
    {
        cout << "Waiting for client connection..." << endl;
        *fout << "Waiting for client connection..." << endl;
        res = 0;
    }
}

Out::~Out()
{
    delete fout;
    fout = NULL;
}
