#include <unistd.h>
#include <iostream>
#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QMap>
#include <QDataStream>
#include <QByteArray>
#include <QString>
#include <QTimer>

#include "main.hh"

using namespace std;

// SENDER GLOBAL VARIABLES

NetSocket * sendSocket;
ChatDialog * ui;

int portMin;
int portMax;

int ourPort;
int senderPort;

// RUMOR PROTOCOL RELATED GLOBVARS
quint32 sequenceNum;
QMap<QString, quint32> want_map; // for keeping track of wants

// HELPER FUNCTIONS
// sender side
QMap<QString, QVariant> assemble_rumor(QString);
QMap<QString, QMap<QString, quint32> > assemble_status();

// receiver side
void parseMapRecvd(QMap<QString, QVariant>);
void parseMapRecvd(QMap<QString, QMap<QString, quint32> >);
void parseRumor(QMap<QString, QVariant>);
void parseStatus(QMap<QString, QMap<QString, quint32> >);
void parseStatus(QVariantMap);

QByteArray serializeRumor(QString);
QByteArray serializeStatus();
QVariantMap deserializeToMap(QByteArray);

QByteArray reserializeRumor(QMap<QString, QVariant>);

bool sendDatagram(QByteArray); // sends to all
bool sendDatagramToNeighbor(QByteArray); // sends only to a neighbor (used to rumermonger)
bool sendDatagramBack(QByteArray); // sends only to a neighbor (used to rumermonger)

void passOnRumor(QMap<QString, QVariant>);
void sendBackStatus();

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

// ****************************************************************************
// ASSEMBLY FUNCTIONS
// ****************************************************************************

// function to assemble rumor messages using message text
QMap<QString, QVariant> assemble_rumor(QString message_text) {
	QMap<QString, QVariant> rumor;
	rumor.insert("ChatText", message_text);
	rumor.insert("Origin", QString::number(ourPort));
	rumor.insert("SeqNo", sequenceNum);

	sequenceNum++; // move sequence number forward

	return rumor;
}

// function to assemble status messages
QMap<QString, QMap<QString, quint32> > assemble_status() {
	QMap<QString, QMap<QString, quint32> > status;
	status.insert("Want", want_map);

	return status;
}

// ****************************************************************************
// SERIALIZATION AND DESERIALIZATION FUNCTIONS
// ****************************************************************************

QByteArray serializeRumor(QString message_text) {
	QMap<QString, QVariant> message_map = assemble_rumor(message_text);

	QByteArray data;
	QDataStream * stream = new QDataStream(&data, QIODevice::WriteOnly);

	(*stream) << message_map;
	delete stream;

	return data;
}

QByteArray reserializeRumor(QMap<QString, QVariant> rumor) {
	QByteArray data;
	QDataStream * stream = new QDataStream(&data, QIODevice::WriteOnly);

	(*stream) << rumor;
	delete stream;

	return data;
}

QByteArray serializeStatus() {
	QMap<QString, QMap<QString, quint32> > message_map = assemble_status();

	QByteArray data;
	QDataStream * stream = new QDataStream(&data, QIODevice::WriteOnly);

	(*stream) << message_map;
	delete stream;

	return data;
}

QVariantMap deserializeToMap(QByteArray data) {
	QVariantMap message_map;

	QDataStream stream(&data, QIODevice::ReadOnly);
	stream >> message_map;

	return message_map;
}

// ****************************************************************************
// PARSE FUNCTIONS
// ****************************************************************************

void parseMapRecvd(QVariantMap map_recvd) {
	if(map_recvd.contains("ChatText")) {
		map_recvd = QMap<QString, QVariant>(map_recvd);
		parseRumor(map_recvd);
	}
	else if(map_recvd.contains("Want")) {
		parseStatus(map_recvd);
	}
	else {
		qDebug() << "Map is neither proper status nor rumor message";
	}
}

void parseRumor(QMap<QString, QVariant> rumor) {
	bool newMessage = false;
	// check for origin key
	if(rumor.contains("Origin") && rumor.contains("SeqNo")) {
		QString origin = rumor["Origin"].toString();
		quint32 sequence = rumor["SeqNo"].toUInt();

		qDebug() << QString("Received rumor from origin " + origin);
		// check to see if sending to itself
		if(QString::number(ourPort) != origin) {
			// we have received messages from this origin before, need to update sequence number
			if(want_map.contains(origin)) {
				if(sequence == want_map[origin]) {
					newMessage = true;
				}
				want_map[origin] = sequence+1;
			}
			else { // add this origin to the table
				newMessage = true;
				want_map.insert(origin, sequence+1); // want the next message
			}
		}
		else {
			qDebug() << "Message is from myself, not printing";
		}

		// if the message is new
		if(newMessage) {
			// add to chat
			QString message_text = rumor["ChatText"].toString();
			ui->appendString(QString(origin + ": " + message_text));

			passOnRumor(rumor);
			sendSocket->waitForStatusResponse(rumor);
			sendBackStatus();
		}
	}
	else {
		qDebug() << "Missing SeqNo or Origin field";
	}
}

void passOnRumor(QMap<QString, QVariant> rumor) {
	QByteArray rumorBytes = reserializeRumor(rumor);

	bool rumorSendSuccess = sendDatagramToNeighbor(rumorBytes);

	if (!rumorSendSuccess) {
		qDebug() << "Rumor datagram failed to send, need to handle";
	}
}

void parseStatus(QVariantMap status) {
	qDebug() << "Status parsing code";
}

// ****************************************************************************
// SEND FUNCTIONS
// ****************************************************************************

bool sendDatagram(QByteArray data) {
	int sent;
	for (int port = portMin; port <= portMax; port++) {
		sent = sendSocket->writeDatagram(data, QHostAddress::LocalHost, port);

		if(sent == -1) return false; // error code
	}
	return true;
}

bool sendDatagramToNeighbor(QByteArray data) {
	int sent;

	int neighborPort;

	if(senderPort == portMin) {
		neighborPort = senderPort + 1;
	}
	else if (senderPort == portMax) {
		neighborPort = senderPort - 1;
	}
	else {
		if(qrand() > .5*RAND_MAX) {
			neighborPort = senderPort + 1;
		}
		else {
			neighborPort = senderPort - 1;
		}
	}

	sent = sendSocket->writeDatagram(data, QHostAddress::LocalHost, neighborPort);

	if(sent == -1) return false; // error code

	qDebug() << QString("Successfully rumormongered message to neighbor on port " + QString::number(neighborPort));

	return true;
}

bool sendDatagramBack(QByteArray data) {
	int sent;

	sent = sendSocket->writeDatagram(data, QHostAddress::LocalHost, senderPort);

	if(sent == -1) return false; // error code

	qDebug() << QString("Sent back status to sender at port " + QString::number(senderPort));

	return true;
}

void sendBackStatus() {
	QByteArray statusToSend = serializeStatus();
	bool statusSendSuccess = sendDatagramBack(statusToSend);

	if (!statusSendSuccess) {
		qDebug() << "Status datagram failed to send, need to handle";
	}
}

// ****************************************************************************
// CHAT UI FUNCTIONS
// ****************************************************************************

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

// RETURN KEY HANDLER
void ChatDialog::gotReturnPressed()
{
	textview->append(QString::number(ourPort) + ": " + textline->text());

	QString message_text = textline->text(); // get message text
	QByteArray rumorToSend = serializeRumor(message_text); // serialize into bytestream

	bool rumorSendSuccess = sendDatagram(rumorToSend);

	if (!rumorSendSuccess) {
		qDebug() << "Rumor datagram failed to send, need to handle";
	}

	// Clear the textline to get ready for the next input message.
	textline->clear();
}

void ChatDialog::appendString(QString str) {
	textview->append(str);
}

// ****************************************************************************
// SOCKET STUFF
// ****************************************************************************

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
         quint16 temp;

         sendSocket->readDatagram(datagram.data(), datagram.size(), &sender, &temp);

		 senderPort = int(temp);

		 // deserialize the byte array to a map
		 QMap<QString, QVariant> deserialized = deserializeToMap(datagram);

		 if (deserialized.empty()) {
			 qDebug() << "Empty status map";
		 }
		 else {
			 parseMapRecvd(deserialized);
		 }
    }
}

bool NetSocket::bind()
{
	// Try to bind to each of the range myPortMin..myPortMax in turn.
	for (int p = myPortMin; p <= myPortMax; p++) {
		if (QUdpSocket::bind(p)) {
			qDebug() << "bound to UDP port " << p;
			ourPort = p;
			// connect receive signal to readDatagram signal handler
			connect(this, SIGNAL(readyRead()), this, SLOT(recvDatagram()));

			timer = new QTimer(this);
			connect(timer, SIGNAL(timeout()), this, SLOT(timeoutHandler()));
			return true;
		}
	}

	qDebug() << "Oops, no ports in my default range " << myPortMin
		<< "-" << myPortMax << " available";
	return false;
}

void NetSocket::waitForStatusResponse(QMap<QString, QVariant> rumor) {
    timer->start(1000);
}

void NetSocket::timeoutHandler() {
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

	// set sequenceNum to 0 bc no messages sent yet
	sequenceNum = 0;

	// Enter the Qt main loop; everything else is event driven
	return app.exec();
}
