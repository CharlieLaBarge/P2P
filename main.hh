#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QUdpSocket>
#include <QMap>
#include <QTimer>

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
	void waitForStatusResponse(QMap<QString, QVariant> rumor);

public slots:
	void recvDatagram();
	void timeoutHandler();
	void antientropyTimeoutHandler();

private:
	int myPortMin, myPortMax;
	QTimer * timer;
	QTimer * entropyTimer;
};

#endif // P2PAPP_MAIN_HH
