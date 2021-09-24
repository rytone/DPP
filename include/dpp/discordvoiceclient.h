/************************************************************************************
 *
 * D++, A Lightweight C++ library for Discord
 *
 * Copyright 2021 Craig Edwards and D++ contributors 
 * (https://github.com/brainboxdotcc/DPP/graphs/contributors)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/
#pragma once

#include <dpp/export.h>

#include <errno.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <io.h>
#else
#include <netinet/in.h>
#include <resolv.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include <dpp/json_fwd.hpp>
#include <dpp/wsclient.h>
#include <dpp/dispatcher.h>
#include <dpp/cluster.h>
#include <queue>
#include <thread>
#include <deque>
#include <mutex>

using json = nlohmann::json;

struct OpusDecoder;
struct OpusEncoder;
struct OpusRepacketizer;

namespace dpp {

// Forward declaration
class cluster;

#define AUDIO_TRACK_MARKER (uint16_t)0xFFFF

/** @brief Implements a discord voice connection.
 * Each discord_voice_client connects to one voice channel and derives from a websocket client.
 */
class CoreExport discord_voice_client : public websocket_client
{
	/** Mutex for outbound packet stream */
	std::mutex stream_mutex;

	/** Mutex for message queue */
	std::mutex queue_mutex;

	/** Queue of outbound messages */
	std::deque<std::string> message_queue;

	/** Thread this connection is executing on */
	std::thread* runner;

	/** Run shard loop under a thread */
	void ThreadRun();

	/** Last connect time of voice session */
	time_t connect_time;

	/**
	 * @brief IP of UDP/RTP endpoint
	 */
	std::string ip;

	/**
	 * @brief Port number of UDP/RTP endpoint
	 */
	uint16_t port;

	/**
	 * @brief SSRC value 
	 */
	uint64_t ssrc;

	/**
	 * @brief List of supported audio encoding modes
	 */
	std::vector<std::string> modes;

	/** Output buffer. Each string is a UDP packet.
	 * Generally these will be RTP.
	 */
	std::vector<std::string> outbuf;

	/** Input  buffer. Each string is a received UDP
	 * packet. These will usually be RTP.
	 */
	std::vector<std::string> inbuf;

	/** If true, audio packet sending is paused
	 */
	bool paused;

#ifdef HAVE_VOICE
	/** libopus encoder
	 */
	OpusEncoder* encoder;

	/** libopus decoder
	 */
	OpusDecoder* decoder;

	/** libopus repacketizer
	 * (merges frames into one packet)
	 */
	OpusRepacketizer* repacketizer;
#endif

	/** File descriptor for UDP connection
	 */
	SOCKET fd;

	/** Socket address of voice server
	 */
	struct sockaddr_in servaddr;

	/** Secret key for encrypting voice.
	 * If it has been sent, this is non-null and points to a 
	 * sequence of exactly 32 bytes.
	 */
	uint8_t* secret_key;

	/** Sequence number of outbound audio. This is incremented
	 * once per frame sent.
	 */
	uint16_t sequence;

	/** Timestamp value used in outbound audio. Each packet
	 * has the timestamp value which is incremented to match
	 * how many frames are sent.
	 */
	uint32_t timestamp;

	/** This is set to true if we have started sending audio.
	 * When this moves from false to true, this causes the
	 * client to send the 'talking' notification to the websocket.
	 */
	bool sending;

	/** Number of track markers in the buffer. For example if there
	 * are two track markers in the buffer there are 3 tracks.
	 * Special case:
	 * If the buffer is empty, there are zero tracks in the
	 * buffer.
	 */
	uint32_t tracks;

	/** Meta data associated with each track.
	 * Arbitrary string that the user can set via
	 * dpp::discord_voice_client::AddMarker
	 */
	std::vector<std::string> track_meta;

	/** Encoding buffer for opus repacketizer and encode
	 */
	uint8_t encode_buffer[65536];

	/**
	 * @brief Send data to UDP socket immediately.
	 * 
	 * @param data data to send
	 * @param length length of data to send
	 * @return int bytes sent. Will return -1 if we cannot send
	 */
	int UDPSend(const char* data, size_t length);

	/**
	 * @brief Receive data from UDP socket immediately.
	 * 
	 * @param data data to receive
	 * @param max_length size of data receiving buffer
	 * @return int bytes received. -1 if there is an error
	 * (e.g. EAGAIN)
	 */
	int UDPRecv(char* data, size_t max_length);

	/**
	 * @brief This hooks the ssl_client, returning the file
	 * descriptor if we want to send buffered data, or
	 * -1 if there is nothing to send
	 * 
	 * @return int file descriptor or -1
	 */
	int WantWrite();

	/**
	 * @brief This hooks the ssl_client, returning the file
	 * descriptor if we want to receive buffered data, or
	 * -1 if we are not wanting to receive
	 * 
	 * @return int file descriptor or -1
	 */
	int WantRead();

	/**
	 * @brief Called by ssl_client when the socket is ready
	 * for writing, at this point we pick the head item off
	 * the buffer and send it. So long as it doesnt error
	 * completely, we pop it off the head of the queue.
	 */
	void WriteReady();

	/**
	 * @brief Called by ssl_client when there is data to be
	 * read. At this point we insert that data into the
	 * input queue.
	 */
	void ReadReady();

	/**
	 * @brief Send data to the UDP socket, using the buffer.
	 * 
	 * @param packet packet data
	 * @param len length of packet
	 */
	void Send(const char* packet, size_t len);

	/**
	 * @brief Queue a message to be sent via the websocket
	 * 
	 * @param j The JSON data of the message to be sent
	 * @param to_front If set to true, will place the message at the front of the queue not the back
	 * (this is for urgent messages such as heartbeat, presence, so they can take precedence over
	 * chunk requests etc)
	 */
	void QueueMessage(const std::string &j, bool to_front = false);

	/**
	 * @brief Clear the outbound message queue
	 * 
	 */
	void ClearQueue();

	/**
	 * @brief Get the size of the outbound message queue
	 * 
	 * @return The size of the queue
	 */
	size_t GetQueueSize();

	/**
	 * @brief Encode a byte buffer using opus codec.
	 * Multiple opus frames (2880 bytes each) will be encoded into one packet for sending.
	 * 
	 * @param input Input data as raw bytes of PCM data
	 * @param inDataSize Input data length
	 * @param output Output data as an opus encoded packet
	 * @param outDataSize Output data length, should be at least equal to the input size.
	 * Will be adjusted on return to the actual compressed data size.
	 * @return size_t The compressed data size that was encoded.
	 */
	size_t encode(uint8_t *input, size_t inDataSize, uint8_t *output, size_t &outDataSize);

public:

	/** Owning cluster */
	class dpp::cluster* creator;

	/* This needs to be static, we only initialise libsodium once per program start,
	* so initialising it on first use in a voice connection is best.
	*/
	static bool sodium_initialised;

	/** True when the thread is shutting down */
	bool terminating;

	/** Heartbeat interval for sending heartbeat keepalive */
	uint32_t heartbeat_interval;

	/** Last heartbeat */
	time_t last_heartbeat;

	/** Thread ID */
	std::thread::native_handle_type thread_id;

	/** Discord voice session token */
	std::string token;

	/** Discord voice session id */
	std::string sessionid;

	/** Server ID */
	snowflake server_id;

	/** Channel ID */
	snowflake channel_id;

	/** Log a message to whatever log the user is using.
	 * The logged message is passed up the chain to the on_log event in user code which can then do whatever
	 * it wants to do with it.
	 * @param severity The log level from dpp::loglevel
	 * @param msg The log message to output
	 */
	virtual void log(dpp::loglevel severity, const std::string &msg);

	/** Fires every second from the underlying socket I/O loop, used for sending heartbeats */
	virtual void one_second_timer();

	/**
	 * @brief voice client is ready to stream audio.
	 * The voice client is considered ready if it has a secret key.
	 * 
	 * @return true if ready to stream audio
	 */
	bool is_ready();

	/**
	 * @brief Returns true if the voice client is connected to the websocket
	 * 
	 * @return True if connected
	 */
	bool is_connected();

	/**
	 * @brief Returns the connection time of the voice client
	 * 
	 * @return dpp::utility::uptime Detail of how long the voice client has been connected for
	 */
	dpp::utility::uptime get_uptime();

	/** Constructor takes shard id, max shards and token.
	 * @param _cluster The cluster which owns this voice connection, for related logging, REST requests etc
	 * @param _server_id The server id (guild id) to identify the voice connection as
	 * @param _channel_id The channel id to identify the voice connection as
	 * @param _server_id The server id to identify the voice connection as
	 * @param _token The voice session token to use for identifying to the websocket
	 * @param _session_id The voice session id to identify with
	 * @param _host The voice server hostname to connect to (hostname:port format)
	 */
	discord_voice_client(dpp::cluster* _cluster, snowflake _channel_id, snowflake _server_id, const std::string &_token, const std::string &_session_id, const std::string &_host);

	/** Destructor */
	virtual ~discord_voice_client();

	/** Handle JSON from the websocket.
	 * @param buffer The entire buffer content from the websocket client
	 * @returns True if a frame has been handled
	 */
	virtual bool HandleFrame(const std::string &buffer);

	/** Handle a websocket error.
	 * @param errorcode The error returned from the websocket
	 */
	virtual void Error(uint32_t errorcode);

	/** Start and monitor I/O loop */
	void Run();

	/**
	 * @brief Send audio to the voice channel.
	 * 
	 * You should send an audio packet of n11520 bytes.
	 * Note that this function can be costly as it has to opus encode
	 * the PCM audio on the fly, and also encrypt it with libsodium.
	 * 
	 * @note Because this function encrypts and encodes packets before
	 * pushing them onto the output queue, if you have a complete stream
	 * ready to send and know its length it is advisable to call this
	 * method multiple times to enqueue the entire stream audio so that
	 * it is all encoded at once (unless you have set use_opus to false).
	 * Constantly calling this from the dpp::on_voice_buffer_send callback
	 * can and will eat a TON of cpu!
	 * 
	 * @param audio_data Raw PCM audio data. Channels are interleaved,
	 * with each channel's amplitude being a 16 bit value.
	 * 
	 * The audio data should be 48000Khz signed 16 bit audio.
	 * 
	 * @param length The length of the audio data. The length should
	 * be a multiple of 4 (2x 16 bit stero channels) with a maximum
	 * length of 11520, which is a complete opus frame at highest
	 * quality.
	 * @param use_opus Some containers such as .ogg may contain OPUS
	 * encoded data already. In this case, we don't need to encode the
	 * frames using opus here. We can set use_opus to false and bypass the
	 * codec, only applying libsodium to the stream.
	 * 
	 * @note If you set use_opus to false, it is your responsibility to ensure
	 * that packets of data sent to send_audio are correctly repacketized
	 * for streaming, e.g. that audio frames are not too large or contain
	 * an incorrect format. Discord will still expect the same frequency
	 * and bit width of audio and the same signedness.
	 */
	void send_audio(uint16_t* audio_data, const size_t length, bool use_opus = true);

	/**
	 * @brief Pause sending of audio
	 * 
	 * @param pause True to pause, false to resume
	 */
	void pause_audio(bool pause);

	/**
	 * @brief Immediately stop all audio.
	 * Clears the packet queue.
	 */
	void stop_audio();

	/**
	 * @brief Returns true if we are playing audio
	 * 
	 * @return true if audio is playing
	 */
	bool is_playing();

	/**
	 * @brief Get the number of seconds remaining
	 * of the audio output buffer
	 * 
	 * @return float number of seconds remaining 
	 */
	float get_secs_remaining();

	/**
	 * @brief Get the number of tracks remaining
	 * in the output buffer.
	 * This is calculated by the number of track
	 * markers plus one.
	 * @return uint32_t Number of tracks in the
	 * buffer
	 */
	uint32_t get_tracks_remaining();

	/**
	 * @brief Get the time remaining to send the
	 * audio output buffer in hours:minutes:seconds
	 * 
	 * @return dpp::utility::uptime length of buffer
	 */
	dpp::utility::uptime get_remaining();

	/**
	 * @brief Insert a track marker into the audio
	 * output buffer.
	 * A track marker is an arbitrary flag in the
	 * buffer contents that indictes the end of some
	 * block of audio of significance to the sender.
	 * This may be a song from a streaming site, or
	 * some voice audio/speech, a sound effect, or
	 * whatever you choose. You can later skip
	 * to the next marker using the
	 * dpp::discord_voice_client::skip_to_next_marker
	 * function.
	 * @param metadata Arbitrary information related to this
	 * track
	 */
	void insert_marker(const std::string& metadata = "");

	/**
	 * @brief Skip tp the next track marker,
	 * previously inserted by using the
	 * dpp::discord_voice_client::insert_marker
	 * function. If there are no markers in the
	 * output buffer, then this skips to the end
	 * of the buffer and is equivalent to the
	 * dpp::discord_voice_client::stop_audio
	 * function.
	 * @note It is possible to use this function
	 * while the output stream is paused.
	 */
	void skip_to_next_marker();

	/**
	 * @brief Get the metdata string associated with each inserted marker.
	 * 
	 * @return const std::vector<std::string>& list of metadata strings
	 */
	const std::vector<std::string> get_marker_metadata();

	/**
	 * @brief Returns true if the audio is paused.
	 * You can unpause with
	 * dpp::discord_voice_client::pause_audio.
	 * 
	 * @return true if paused
	 */
	bool is_paused();

	/**
	 * @brief Discord external IP detection.
	 * @return std::string Your external IP address
	 * @note This is a blocking operation that waits
	 * for a single packet from Discord's voice servers.
	 */
	std::string discover_ip();
};

};

