/*
    This is a SampVoice project file
    Developer: CyberMor <cyber.mor.2020@gmail.com>

    See more here https://github.com/CyberMor/sampvoice

    Copyright (c) Daniel (CyberMor) 2020 All rights reserved
*/

#include <system/types.hpp>
#include <memory/structures/array.hpp>
#include <memory/structures/bitset.hpp>
#include <memory/structures/pool.hpp>
#include <network/address.hpp>
#include <network/socket.hpp>
#include <other/config.hpp>

#include "command.hpp"
#include "voice.hpp"

#include "main.hpp"

// Types
// ----------------------------------------------------------------

struct Player
{
    std::atomic_bool listener = true;

    std::atomic<udword_t> channels = 0; // Enabled speaker channels

    Array<Bitset<kMaxStreams>, Bits<udword_t>> listeners; // Streams in which the Player speaks
};

struct Stream
{
    std::atomic_bool transiter = true;

    Bitset<kMaxPlayers> listeners; // Players who listen
};

// Global Context
// ----------------------------------------------------------------

static std::atomic_bool gWorkStatus;

static Pool<Player, kMaxPlayers> gPlayers;
static Pool<Stream, kMaxStreams> gStreams;

static bool gControlTrace = false;

// Workers
// ----------------------------------------------------------------

static void ControlWorker() noexcept
{
    alignas (alignof(std::max_align_t))
    ubyte_t buffer[kCommandContentLimit];

    while (gWorkStatus.load(std::memory_order_relaxed))
    {
        if (const ubyte_t command = CommandService::Instance().WaitCommand(buffer, kKeepAliveInterval);
                          command != CommandService::WaitEmpty)
        {
            switch (command)
            {
                case CommandPackets::PlayerCreate:
                {
                    const auto& content = *reinterpret_cast<const PlayerCreate*>(buffer);

                    if (gControlTrace) std::printf("PlayerCreate : player(%hu), key(0x%X)\n",
                        content.player, content.key);

                    if (content.player < kMaxPlayers && content.key != 0)
                    {
                        if (gPlayers.EmplaceAt(content.player, true))
                        {
                            VoiceService::Instance().RegisterPlayer(content.player, content.key);
                        }
                    }

                    break;
                }
                case CommandPackets::PlayerListener:
                {
                    const auto& content = *reinterpret_cast<const PlayerListener*>(buffer);

                    if (gControlTrace) std::printf("PlayerListener : player(%hu), status(%hhu)\n",
                        content.player, content.status);

                    if (content.player < kMaxPlayers)
                    {
                        if (gPlayers.Acquire<0>(content.player))
                        {
                            gPlayers[content.player].listener.store(content.status, std::memory_order_relaxed);
                        }
                    }

                    break;
                }
                case CommandPackets::PlayerSpeaker:
                {
                    const auto& content = *reinterpret_cast<const PlayerSpeaker*>(buffer);

                    if (gControlTrace) std::printf("PlayerSpeaker : player(%hu), channels(0x%X)\n",
                        content.player, content.channels);

                    if (content.player < kMaxPlayers)
                    {
                        if (gPlayers.Acquire<0>(content.player))
                        {
                            gPlayers[content.player].channels.store(content.channels, std::memory_order_relaxed);
                        }
                    }

                    break;
                }
                case CommandPackets::PlayerAttachStream:
                {
                    const auto& content = *reinterpret_cast<const PlayerAttachStream*>(buffer);

                    if (gControlTrace) std::printf("PlayerAttachStream : player(%hu), channels(0x%X), stream(%hu)\n",
                        content.player, content.channels, content.stream);

                    if (content.player < kMaxPlayers && content.channels != 0 && content.stream < kMaxStreams)
                    {
                        if (gPlayers.Acquire<0>(content.player))
                        {
                            BitsetForEach(channel, content.channels)
                            {
                                gPlayers[content.player].listeners[channel].Set(content.stream);
                            }
                        }
                    }

                    break;
                }
                case CommandPackets::PlayerDetachStream:
                {
                    const auto& content = *reinterpret_cast<const PlayerDetachStream*>(buffer);

                    if (gControlTrace) std::printf("PlayerDetachStream : player(%hu), channels(0x%X), stream(%hu)\n",
                        content.player, content.channels, content.stream);

                    if (content.player < kMaxPlayers && content.channels != 0 && content.stream < kMaxStreams)
                    {
                        if (gPlayers.Acquire<0>(content.player))
                        {
                            BitsetForEach(channel, content.channels)
                            {
                                gPlayers[content.player].listeners[channel].Reset(content.stream);
                            }
                        }
                    }

                    break;
                }
                case CommandPackets::PlayerDelete:
                {
                    const auto& content = *reinterpret_cast<const PlayerDelete*>(buffer);

                    if (gControlTrace) std::printf("PlayerDelete : player(%hu)\n", content.player);

                    if (content.player < kMaxPlayers)
                    {
                        if (gPlayers.Acquire<0>(content.player))
                        {
                            VoiceService::Instance().RemovePlayer(content.player);

                            gPlayers.Remove(content.player, false);

                            for (size_t stream = 0; stream != kMaxStreams; ++stream)
                            {
                                if (gStreams.Acquire<0>(stream))
                                {
                                    gStreams[stream].listeners.Reset(content.player);
                                }
                            }
                        }
                    }

                    break;
                }
                case CommandPackets::StreamCreate:
                {
                    const auto& content = *reinterpret_cast<const StreamCreate*>(buffer);

                    if (gControlTrace) std::printf("StreamCreate : stream(%hu)\n", content.stream);

                    if (content.stream < kMaxStreams)
                    {
                        gStreams.EmplaceAt(content.stream, true);
                    }

                    break;
                }
                case CommandPackets::StreamTransiter:
                {
                    const auto& content = *reinterpret_cast<const StreamTransiter*>(buffer);

                    if (gControlTrace) std::printf("StreamTransiter : stream(%hu), status(%hhu)\n",
                        content.stream, content.status);

                    if (content.stream < kMaxStreams)
                    {
                        if (gStreams.Acquire<0>(content.stream))
                        {
                            gStreams[content.stream].transiter.store(content.status, std::memory_order_relaxed);
                        }
                    }

                    break;
                }
                case CommandPackets::StreamAttachListener:
                {
                    const auto& content = *reinterpret_cast<const StreamAttachListener*>(buffer);

                    if (gControlTrace) std::printf("StreamAttachListener : stream(%hu), player(%hu)\n",
                        content.stream, content.player);

                    if (content.stream < kMaxStreams && content.player < kMaxPlayers)
                    {
                        if (gStreams.Acquire<0>(content.stream))
                        {
                            gStreams[content.stream].listeners.Set(content.player);
                        }
                    }

                    break;
                }
                case CommandPackets::StreamDetachListener:
                {
                    const auto& content = *reinterpret_cast<const StreamDetachListener*>(buffer);

                    if (gControlTrace) std::printf("StreamDetachListener : stream(%hu), player(%hu)\n",
                        content.stream, content.player);

                    if (content.stream < kMaxStreams && content.player < kMaxPlayers)
                    {
                        if (gStreams.Acquire<0>(content.stream))
                        {
                            gStreams[content.stream].listeners.Reset(content.player);
                        }
                    }

                    break;
                }
                case CommandPackets::StreamDelete:
                {
                    const auto& content = *reinterpret_cast<const StreamDelete*>(buffer);

                    if (gControlTrace) std::printf("StreamDelete : stream(%hu)\n", content.stream);

                    if (content.stream < kMaxStreams)
                    {
                        if (gStreams.Acquire<0>(content.stream))
                        {
                            gStreams.Remove(content.stream, false);

                            for (size_t player = 0; player != kMaxPlayers; ++player)
                            {
                                if (gPlayers.Acquire<0>(player))
                                {
                                    for (size_t channel = 0; channel != Bits<udword_t>; ++channel)
                                    {
                                        gPlayers[player].listeners[channel].Reset(content.stream);
                                    }
                                }
                            }
                        }
                    }

                    break;
                }
                default:
                {
                    return;
                }
            }
        }

        VoiceService::Instance().Tick();
    }
}

static void VoiceWorker() noexcept
{
    ubyte_t streams_sizes[kMaxPlayers] = {};
    uword_t streams_datas[kMaxPlayers][kMaximumStreamsInPacket + 1];

    alignas (alignof(std::max_align_t))
    ubyte_t buffer[2 * kVoicePacketLimit];
    uword_t sender;

    while (gWorkStatus.load(std::memory_order_relaxed))
    {
        const  adr_t data = buffer + kVoicePacketLimit;
        const size_t size = VoiceService::Instance().ReceivePacket(data, sender);
        if (size == 0) break; // Socket Error

        if (gPlayers.Acquire(sender))
        {
            udword_t channels = gPlayers[sender].channels.load(std::memory_order_relaxed);
            channels &= reinterpret_cast<const IncomingVoiceHeader*>(data)->channels;
            if (channels != 0)
            {
                const udword_t packet = reinterpret_cast<const IncomingVoiceHeader*>(data)->packet;

                const  adr_t voice_data = data + sizeof(IncomingVoiceHeader);
                const size_t voice_size = size - sizeof(IncomingVoiceHeader);

                const size_t max_streams = ((kVoicePacketLimit -
                    sizeof(OutcomingVoiceHeader)) - voice_size) / sizeof(uword_t) - 1;

                // Receivers List Formation ...
                // ----------------------------------------------------------------

                Bitset<kMaxStreams> streams;
                BitsetForEach(channel, channels) streams |= gPlayers[sender].listeners[channel];
                streams.ForEach([&](const size_t stream) noexcept -> void
                {
                    if (gStreams.Acquire(stream))
                    {
                        if (gStreams[stream].transiter.load(std::memory_order_relaxed))
                        {
                            gStreams[stream].listeners.ForEach([&](const size_t listener) noexcept -> void
                            {
                                if (listener != sender) // don't send traffic back to sender
                                {
                                    if (gPlayers.Acquire(listener))
                                    {
                                        if (gPlayers[listener].listener.load(std::memory_order_relaxed))
                                        {
                                            if (streams_sizes[listener] != max_streams)
                                            {
                                                streams_datas[listener][streams_sizes[listener]++] = stream;
                                            }
                                        }

                                        gPlayers.Release(listener);
                                    }
                                }
                            });
                        }

                        gStreams.Release(stream);
                    }
                });

                // Sending ...
                // ----------------------------------------------------------------

                for (size_t player = 0; player != kMaxPlayers; ++player)
                {
                    if (streams_sizes[player] != 0)
                    {
                        streams_datas[player][streams_sizes[player]++] = None<uword_t>;

                        const size_t header_size = sizeof(OutcomingVoiceHeader) +
                            streams_sizes[player] * sizeof(uword_t);

                        const  adr_t packet_data = voice_data - header_size;
                        const size_t packet_size = voice_size + header_size;

                        if constexpr (HostEndian != NetEndian)
                        {
                            for (auto stream = streams_datas[player]; *stream != None<uword_t>; ++stream)
                                utils::bswap(stream);
                        }

                        std::memcpy(packet_data + sizeof(OutcomingVoiceHeader),
                            streams_datas[player], streams_sizes[player] * sizeof(uword_t));

                        reinterpret_cast<OutcomingVoiceHeader*>(packet_data)->packet = packet;
                        reinterpret_cast<OutcomingVoiceHeader*>(packet_data)->source = sender;

                        if constexpr (HostEndian != NetEndian)
                        {
                            utils::bswap(&reinterpret_cast<OutcomingVoiceHeader*>(packet_data)->packet);
                            utils::bswap(&reinterpret_cast<OutcomingVoiceHeader*>(packet_data)->source);
                        }

                        VoiceService::Instance().SendPacket(player, packet_data, packet_size);

                        streams_sizes[player] = 0;
                    }
                }
            }

            gPlayers.Release(sender);
        }
    }
}

// Launcher
// ----------------------------------------------------------------

static void help(const cstr_t program) noexcept
{
    std::printf("-------------------------------------------" "\n");
    std::printf("  Use  : \"%s\" [--trace]"                   "\n", program);
    std::printf(" Flags : --trace - print incoming commands"  "\n");
    std::printf("-------------------------------------------" "\n");
}

int main(const int argc, const char* const* const argv) noexcept
{
    for (int i = 1; i != argc; ++i)
    {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0 ||
            std::strcmp(argv[i], "-?")     == 0 || std::strcmp(argv[i], "/?") == 0)
        {
            return help(*argv), 0;
        }
        else if (std::strcmp(argv[i], "--trace") == 0)
        {
            gControlTrace = true;
        }
        else
        {
            std::printf("Unknown option '%s'.\n", argv[i]);
            return help(*argv), 0;
        }
    }

    std::printf("Voice Server %hhu.%hhu.%hu starting...\n",
        GetVersionMajor(kCurrentVersion),
        GetVersionMinor(kCurrentVersion),
        GetVersionPatch(kCurrentVersion));

    if (!SocketLibraryStartup())
    {
        std::printf("Failed to initialize socket library.\n");
        return -1;
    }

    // Config
    // -----------------------------------------------

    enum : size_t
    {
        PARAMETER_CONTROL_HOST,
        PARAMETER_CONTROL_PORT,
        PARAMETER_COMMAND_HOST,
        PARAMETER_COMMAND_PORT,
        PARAMETER_VOICE_HOST,
        PARAMETER_VOICE_PORT,
        PARAMETER_WORKERS,

        PARAMETERS_COUNT
    };

    std::printf("Loading 'voice.cfg' file...\n");

    Config config;
    {
        config.Reserve(PARAMETERS_COUNT);

        config.Register(PARAMETER_CONTROL_HOST, "control_host");
        config.Register(PARAMETER_CONTROL_PORT, "control_port");
        config.Register(PARAMETER_COMMAND_HOST, "command_host");
        config.Register(PARAMETER_COMMAND_PORT, "command_port");
        config.Register(PARAMETER_VOICE_HOST, "voice_host");
        config.Register(PARAMETER_VOICE_PORT, "voice_port");
        config.Register(PARAMETER_WORKERS, "workers");

        if (config.Load("voice.cfg") < 0)
        {
            std::printf("Failed to load 'voice.cfg'.\n");
            return -1;
        }
    }

    while (true)
    {
        // Command Service
        // -----------------------------------------------

        {
            IPv4Address control = IPv4Address::Empty();
            IPv4Address command = IPv4Address::Loopback(2020);

            if (config.HasParameter(PARAMETER_CONTROL_HOST))
                control.SetHost(config.GetValueByIndex(PARAMETER_CONTROL_HOST).c_str());
            if (config.HasParameter(PARAMETER_CONTROL_PORT))
                control.SetPort(std::stoi(config.GetValueByIndex(PARAMETER_CONTROL_PORT)));

            if (config.HasParameter(PARAMETER_COMMAND_HOST))
                command.SetHost(config.GetValueByIndex(PARAMETER_COMMAND_HOST).c_str());
            if (config.HasParameter(PARAMETER_COMMAND_PORT))
                command.SetPort(std::stoi(config.GetValueByIndex(PARAMETER_COMMAND_PORT)));

            if (char host[IPv4Address::HostLengthLimit + 1]; control.PrintHost(host))
                std::printf("Waiting for connection from control server '%s:%hu'...\n",
                    host, control.GetPort<true>());

            if (!CommandService::Instance().Initialize(command, control))
            {
                std::printf("Failed to initialize command service.\n");
                return -1;
            }

            std::printf("Connection to control server established.\n");
        }

        // Voice Service
        // -----------------------------------------------

        {
            IPv4Address voice = IPv4Address::Any(2020);

            if (config.HasParameter(PARAMETER_VOICE_HOST))
                voice.SetHost(config.GetValueByIndex(PARAMETER_VOICE_HOST).c_str());
            if (config.HasParameter(PARAMETER_VOICE_PORT))
                voice.SetPort(std::stoi(config.GetValueByIndex(PARAMETER_VOICE_PORT)));

            if (char host[IPv4Address::HostLengthLimit + 1]; voice.PrintHost(host))
                std::printf("Opening voice service at '%s:%hu'...\n",
                    host, voice.GetPort<true>());

            if (!VoiceService::Instance().Initialize(voice, stdout))
            {
                std::printf("Failed to initialize voice service.\n");
                CommandService::Instance().Deinitialize();
                return -1;
            }

            std::printf("Voice service opened.\n");
        }

        // Workers
        // -----------------------------------------------

        int workers = 0;

        if (config.HasParameter(PARAMETER_WORKERS))
        {
            workers = std::stoi(config.GetValueByIndex(PARAMETER_WORKERS));
            if (workers < 0) workers = 0;
        }

        if (workers == 0)
        {
            workers = static_cast<int>(std::thread::hardware_concurrency()) - 1;
            if (workers < 1) workers = 1;
        }

        std::vector<std::thread> threads;
        threads.reserve(workers);

        // Launch
        // -----------------------------------------------

        gWorkStatus.store(true, std::memory_order_release);

        std::printf("Launching %d voice workers...\n", workers);

        for (int i = 0; i != workers; ++i)
        {
            threads.emplace_back([]() noexcept -> void { VoiceWorker(); });
        }

        ControlWorker();

        // Stopping and Deinitialization
        // -----------------------------------------------

        std::printf("Finishing work...\n");

        gWorkStatus.store(false, std::memory_order_release);

        CommandService::Instance().Deinitialize();
        VoiceService::Instance().Deinitialize();

        for (auto& thread : threads)
        {
            if (thread.joinable())
                thread.join();
        }

        gPlayers.Clear();
        gStreams.Clear();
    }

    SocketLibraryCleanup();

    return 0;
}
