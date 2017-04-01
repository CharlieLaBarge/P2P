#include <unistd.h>
#include <iostream>
#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QMap>
#include <QDataStream>
#include <QByteArray>

#include "main.hh"

using namespace std;


NetSocket * sendSocket;
ChatDialog * ui;

int portMin;
int portMax;

int senderPort;

QByteArray serializeMessage(QString);
QMap<QString, QString> deserializeToMap(QByteArray);
bool sendDatagram(QByteArray);
void recvDatagram();

// helper function for printing bytestreams from http://stackoverflow.com/questions/12209046/how-do-i-see-the-contents-of-qt-objects-qbytearray-during-debugging
QString toDebug(const QByteArray & line) {
    QString s;
    uchar c;

    for ( int i=0 ; i < line.size() ; i++ ){
        c = line[i];
        if ( c >= 0x20 and c <= 126 ) {
            s.append(c);
        } else {
            s.append(QString("<%1>").arg(c, 2, 16, QChar('0')));
        }
    }
    return s;
}

QByteArray serializeMessage(QString message_text) {
	QString chattext_type = QString("ChatText");
	QString sender_id_type = QString("SenderID");

	QMap<QString, QString> message_map;
	message_map.insert(chattext_type, message_text);

	QString sender = QString(senderPort);
	message_map.insert(sender_id_type, sender);

	qDebug() << message_map;

	QByteArray data;
	QDataStream * stream = new QDataStream(&data, QIODevice::WriteOnly);

	(*stream) << message_map;
	delete stream;

	return data;
}

QMap<QString, QString> deserializeToMap(QByteArray data) {
	QMap<QString, QString> message_map;

	QDataStream stream(&data, QIODevice::ReadOnly);
	stream >> message_map;

	return message_map;
}

ChatDialog::ChatDialog()
{
	setWindowTitle("P2Papp");

	// Read-only text box where we display messages from everyone.
	// This widget expands both horizontally and vertically.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

	// Small text-entry box the user can enter messages.
	// This widget normally expands only horizontally,
	// leaving extra vertical space for the textview widget.
	//
	// You might change this into a read/write QTextEdit,
	// so that the user can easily enter multi-line messages.
	textline = new QLineEdit(this);

	// Lay out the widgets to appear in the main window.
	// For Qt widget and layout concepts see:
	// http://doc.qt.nokia.com/4.7-snapshot/widgets-and-layouts.html
	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(textview);
	layout->addWidget(textline);
	setLayout(layout);

	// Register a callback on the textline's returnPressed signal
	// so that we can send the message entered by the user.
	connect(textline, SIGNAL(returnPressed()),
		this, SLOT(gotReturnPressed()));
}

void ChatDialog::gotReturnPressed()
{
	// Initially, just echo the string locally.
	// Insert some networking code here...
	qDebug() << "FIX: send message to other peers: " << textline->text();
	textview->append(QString(senderPort) + ": " + textline->text());


	QString message_text = textline->text();
	QByteArray test = serializeMessage(message_text);

	cout << sendDatagram(test);

	// Clear the textline to get ready for the next input message.
	textline->clear();
}

void ChatDialog::parseMapRecvd(QMap<QString, QString> map_recvd) {
	if(map_recvd.contains("ChatText")) {
		if(map_recvd.contains("SenderID")) {
			if(QString(senderPort) != map_recvd["SenderID"]) {
				QString message_text = map_recvd["ChatText"];
				textview->append(map_recvd["SenderID"] + ": " + message_text);
			}
			else {
				qDebug() << "Message is from myself, not printing";
			}
		}
		else {
			qDebug() << "No sender id sent";
		}
	}
	else {
		qDebug() << "No ChatText entry in map";
	}
}

NetSocket::NetSocket()
{
	// Pick a range of four UDP ports to try to allocate by default,
	// computed based on my Unix user ID.
	// This makes it trivial for up to four P2Papp instances per user
	// to find each other on the same host,
	// barring UDP port conflicts with other applications
	// (which are quite possible).
	// We use the range from 32768 to 49151 for this purpose.
	myPortMin = 32768 + (getuid() % 4096)*4;
	myPortMax = myPortMin + 3;

	// setup global port limits
	portMin = myPortMin;
	portMax = myPortMax;
}

void NetSocket::recvDatagram() {
	while (sendSocket->hasPendingDatagrams())
     {
         QByteArray datagram;
         datagram.resize(sendSocket->pendingDatagramSize());
         QHostAddress sender;
         quint16 senderPort;

         sendSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

		 // deserialize the byte array to a map
		 QMap<QString, QString> deserialized = deserializeToMap(datagram);

		 qDebug() << deserialized;
		 // call the UI's parsing algo
		 ui->parseMapRecvd(deserialized);
    }
}


bool NetSocket::bind()
{
	// Try to bind to each of the range myPortMin..myPortMax in turn.
	for (int p = myPortMin; p <= myPortMax; p++) {
		if (QUdpSocket::bind(p)) {
			qDebug() << "bound to UDP port " << p;
			senderPort = p;
			// connect receive signal to readDatagram signal handler
			connect(this, SIGNAL(readyRead()), this, SLOT(recvDatagram()));
			return true;
		}
	}

	qDebug() << "Oops, no ports in my default range " << myPortMin
		<< "-" << myPortMax << " available";
	return false;
}

bool sendDatagram(QByteArray data) {
	for (int port = portMin; port <= portMax; port++) {
		sendSocket->writeDatagram(data, QHostAddress::LocalHost, port);
	}
	return true;
}

int main(int argc, char **argv)
{
	// Initialize Qt toolkit
	QApplication app(argc,argv);

	// Create an initial chat dialog window
	ChatDialog dialog;
	dialog.show();

	// Create a UDP network socket
	NetSocket sock;
	if (!sock.bind())
		exit(1);

	sendSocket = &sock; // setup send socket
	ui = &dialog;

	// Enter the Qt main loop; everything else is event driven
	return app.exec();
}
