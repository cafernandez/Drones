/*=====================================================================
 
 QGroundControl Open Source Ground Control Station
 
 (c) 2009 - 2014 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 
 This file is part of the QGROUNDCONTROL project
 
 QGROUNDCONTROL is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 QGROUNDCONTROL is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with QGROUNDCONTROL. If not, see <http://www.gnu.org/licenses/>.
 
 ======================================================================*/

#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>
#include <QDir>
#include <QTimer>

#include "UASInterface.h"
#include "QGCLoggingCategory.h"

Q_DECLARE_LOGGING_CATEGORY(FileManagerLog)

class Vehicle;

class FileManager : public QObject
{
    Q_OBJECT
    
public:
    FileManager(QObject* parent, Vehicle* vehicle);
    
    /// These methods are only used for testing purposes.
    bool _sendCmdTestAck(void) { return _sendOpcodeOnlyCmd(kCmdNone, kCOAck); };
    bool _sendCmdTestNoAck(void) { return _sendOpcodeOnlyCmd(kCmdTestNoAck, kCOAck); };
    
    /// Timeout in msecs to wait for an Ack time come back. This is public so we can write unit tests which wait long enough
    /// for the FileManager to timeout.
    static const int ackTimerTimeoutMsecs = 10000;

	/// Downloads the specified file.
	///     @param from File to download from UAS, fully qualified path
	///     @param downloadDir Local directory to download file to
	void downloadPath(const QString& from, const QDir& downloadDir);
	
	/// Stream downloads the specified file.
	///     @param from File to download from UAS, fully qualified path
	///     @param downloadDir Local directory to download file to
	void streamPath(const QString& from, const QDir& downloadDir);
	
	/// Lists the specified directory. Emits listEntry signal for each entry, followed by listComplete signal.
	///		@param dirPath Fully qualified path to list
	void listDirectory(const QString& dirPath);
	
    /// Upload the specified file to the specified location
    void uploadPath(const QString& toPath, const QFileInfo& uploadFile);
    
signals:
    // Signals associated with the listDirectory method
    
    /// Signalled to indicate a new directory entry was received.
    void listEntry(const QString& entry);
    
    // Signals associated with all commands
    
    /// Signalled after a command has completed
    void commandComplete(void);
    
    /// Signalled when an error occurs during a command. In this case a commandComplete signal will
    /// not be sent.
    void commandError(const QString& msg);
    
    /// Signalled during a lengthy command to show progress
    ///     @param value Amount of progress: 0.0 = none, 1.0 = complete
    void commandProgress(int value);

public slots:
    void receiveMessage(mavlink_message_t message);
	
private slots:
	void _ackTimeout(void);

private:
    /// @brief This is the fixed length portion of the protocol data. Trying to pack structures across differing compilers is
    /// questionable, so we pad the structure ourselves to 32 bit alignment which should get us what we want.
    struct RequestHeader
        {
            uint16_t    seqNumber;      ///< sequence number for message
            uint8_t     session;        ///< Session id for read and write commands
            uint8_t     opcode;         ///< Command opcode
            uint8_t     size;           ///< Size of data
            uint8_t     req_opcode;     ///< Request opcode returned in kRspAck, kRspNak message
            uint8_t     burstComplete;  ///< Only used if req_opcode=kCmdBurstReadFile - 1: set of burst packets complete, 0: More burst packets coming.
            uint8_t     padding;        ///< 32 bit aligment padding
            uint32_t    offset;         ///< Offsets for List and Read commands
        };

    struct Request
    {
        struct RequestHeader hdr;

        // We use a union here instead of just casting (uint32_t)&payload[0] to not break strict aliasing rules
        union {
            // The entire Request must fit into the payload member of the mavlink_file_transfer_protocol_t structure. We use as many leftover bytes
            // after we use up space for the RequestHeader for the data portion of the Request.
            uint8_t data[sizeof(((mavlink_file_transfer_protocol_t*)0)->payload) - sizeof(RequestHeader)];
			
            // File length returned by Open command
            uint32_t openFileLength;

            // Length of file chunk written by write command
            uint32_t writeFileLength;
        };
    };

    enum Opcode
	{
		kCmdNone,				///< ignored, always acked
		kCmdTerminateSession,	///< Terminates open Read session
		kCmdResetSessions,		///< Terminates all open Read sessions
		kCmdListDirectory,		///< List files in <path> from <offset>
		kCmdOpenFileRO,			///< Opens file at <path> for reading, returns <session>
		kCmdReadFile,			///< Reads <size> bytes from <offset> in <session>
		kCmdCreateFile,			///< Creates file at <path> for writing, returns <session>
		kCmdWriteFile,			///< Writes <size> bytes to <offset> in <session>
		kCmdRemoveFile,			///< Remove file at <path>
		kCmdCreateDirectory,	///< Creates directory at <path>
		kCmdRemoveDirectory,	///< Removes Directory at <path>, must be empty
		kCmdOpenFileWO,			///< Opens file at <path> for writing, returns <session>
		kCmdTruncateFile,		///< Truncate file at <path> to <offset> length
		kCmdRename,				///< Rename <path1> to <path2>
		kCmdCalcFileCRC32,		///< Calculate CRC32 for file at <path>
		kCmdBurstReadFile,      ///< Burst download session file
		
		kRspAck = 128,          ///< Ack response
		kRspNak,                ///< Nak response

        // Used for testing only, not part of protocol
        kCmdTestNoAck,          ///< ignored, ack not sent back, should timeout waiting for ack
	};
	
	/// @brief Error codes returned in Nak response PayloadHeader.data[0].
	enum ErrorCode
    {
		kErrNone,
		kErrFail,                   ///< Unknown failure
		kErrFailErrno,              ///< errno sent back in PayloadHeader.data[1]
		kErrInvalidDataSize,		///< PayloadHeader.size is invalid
		kErrInvalidSession,         ///< Session is not currently open
		kErrNoSessionsAvailable,	///< All available Sessions in use
		kErrEOF,                    ///< Offset past end of file for List and Read commands
        kErrUnknownCommand,          ///< Unknown command opcode
        kErrFailFileExists,         ///< File exists already
        kErrFailFileProtected       ///< File is write protected
    };

    enum OperationState
        {
            kCOIdle,		// not doing anything
            kCOAck,			// waiting for an Ack
            kCOList,		// waiting for List response
            kCOOpenRead,    // waiting for Open response followed by Read download
			kCOOpenBurst,   // waiting for Open response, followed by Burst download
            kCORead,		// waiting for Read response
			kCOBurst,		// waiting for Burst response
            kCOWrite,       // waiting for Write response
            kCOCreate,      // waiting for Create response
        };
    
    bool _sendOpcodeOnlyCmd(uint8_t opcode, OperationState newOpState);
    void _setupAckTimeout(void);
    void _clearAckTimeout(void);
    void _emitErrorMessage(const QString& msg);
    void _emitListEntry(const QString& entry);
    void _sendRequest(Request* request);
    void _fillRequestWithString(Request* request, const QString& str);
    void _openAckResponse(Request* openAck);
    void _downloadAckResponse(Request* readAck, bool readFile);
    void _listAckResponse(Request* listAck);
    void _createAckResponse(Request* createAck);
    void _writeAckResponse(Request* writeAck);
    void _writeFileDatablock(void);
    void _sendListCommand(void);
    void _sendResetCommand(void);
    void _closeDownloadSession(bool success);
    void _closeUploadSession(bool success);
	void _downloadWorker(const QString& from, const QDir& downloadDir, bool readFile);
    
    static QString errorString(uint8_t errorCode);

    OperationState  _currentOperation;              ///< Current operation of state machine
    QTimer          _ackTimer;                      ///< Used to signal a timeout waiting for an ack
    
    Vehicle*        _vehicle;
    LinkInterface*  _dedicatedLink; ///< Link to use for communication
    
    uint16_t _lastOutgoingSeqNumber; ///< Sequence number sent in last outgoing packet

    unsigned    _listOffset;    ///< offset for the current List operation
    QString     _listPath;      ///< path for the current List operation
    
    uint8_t     _activeSession;             ///< currently active session, 0 for none
    
    uint32_t    _readOffset;                ///< current read offset
    
    uint32_t    _writeOffset;               ///< current write offset
    uint32_t    _writeSize;                 ///< current write data size
    uint32_t    _writeFileSize;             ///< Size of file being uploaded
    QByteArray  _writeFileAccumulator;      ///< Holds file being uploaded
    
    uint32_t    _downloadOffset;            ///< current download offset
    QByteArray  _readFileAccumulator;       ///< Holds file being downloaded
    QDir        _readFileDownloadDir;       ///< Directory to download file to
    QString     _readFileDownloadFilename;  ///< Filename (no path) for download file
    uint32_t    _downloadFileSize;          ///< Size of file being downloaded

    uint8_t     _systemIdQGC;               ///< System ID for QGC
    uint8_t     _systemIdServer;            ///< System ID for server
    
    // We give MockLinkFileServer friend access so that it can use the data structures and opcodes
    // to build a mock mavlink file server for testing.
    friend class MockLinkFileServer;
};

#endif // FileManager_H
