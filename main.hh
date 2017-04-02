#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QUdpSocket>
#include <QMap>

class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog();
	void appendString(QString str);

public slots:
	void gotReturnPressed();

private:
	QTextEdit *textview;
	QLineEdit *textline;
};

class NetSocket : public QUdpSocket
{
	Q_OBJECT

public:
	NetSocket();

	// Bind this socket to a P2Papp-specific default port.
	bool bind();

public slots:
	void recvDatagram();

private:
	int myPortMin, myPortMax;
};

#endif // P2PAPP_MAIN_HH
