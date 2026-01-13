using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    public class STUNClient
    {
        // STUN message types
        private const ushort BindingRequest = 0x0001;
        private const ushort BindingResponse = 0x0101;
        private const ushort BindingErrorResponse = 0x0111;

        // STUN attribute types
        private const ushort MappedAddress = 0x0001;
        private const ushort XorMappedAddress = 0x0020;

        // Default STUN servers
        private static readonly string[] DefaultStunServers = new string[]
        {
            "stun.l.google.com:19302",
            "stun1.l.google.com:19302",
            "stun2.l.google.com:19302",
            "stun3.l.google.com:19302",
            "stun4.l.google.com:19302"
        };

        private readonly List<string> stunServers;
        private readonly int timeout;

        public STUNClient(IEnumerable<string> stunServers = null, int timeoutMs = 5000)
        {
            this.stunServers = stunServers != null
                ? new List<string>(stunServers)
                : new List<string>(DefaultStunServers);

            this.timeout = timeoutMs;
        }

        public async Task<IPEndPoint> GetPublicEndPointAsync(int localPort = 0)
        {
            using (var udpClient = new UdpClient(localPort))
            {
                udpClient.Client.ReceiveTimeout = timeout;

                byte[] responseData = null;
                IPEndPoint serverEndPoint = null;

                // Try each STUN server until we get a response
                foreach (string stunServer in stunServers)
                {
                    try
                    {
                        string[] parts = stunServer.Split(':');
                        string host = parts[0];
                        int port = 3478; // Default STUN port
                        if (parts.Length > 1 && !int.TryParse(parts[1], out port))
                            port = 3478;

                        IPAddress[] addresses = await Dns.GetHostAddressesAsync(host);
                        if (addresses.Length == 0) continue;

                        serverEndPoint = new IPEndPoint(addresses[0], port);

                        // Generate STUN Binding Request
                        byte[] requestData = CreateBindingRequest();

                        // Send the request
                        await udpClient.SendAsync(requestData, requestData.Length, serverEndPoint);

                        // Wait for response with timeout
                        var receiveTask = udpClient.ReceiveAsync();

                        if (await Task.WhenAny(receiveTask, Task.Delay(timeout)) == receiveTask)
                        {
                            var result = receiveTask.Result;
                            responseData = result.Buffer;
                            serverEndPoint = result.RemoteEndPoint;
                            break;
                        }
                    }
                    catch (Exception ex)
                    {
                        Logger.Log($"STUN error with server {stunServer}: {ex.Message}");
                        continue;
                    }
                }

                if (responseData == null)
                {
                    throw new TimeoutException("All STUN servers failed to respond");
                }

                // Parse the response to get public endpoint
                return ParseStunResponse(responseData);
            }
        }

        private byte[] CreateBindingRequest()
        {
            // STUN message format:
            // 0                   1                   2                   3
            // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
            // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            // |0 0|     STUN Message Type     |         Message Length        |
            // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            // |                         Magic Cookie                          |
            // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            // |                                                               |
            // |                     Transaction ID (96 bits)                  |
            // |                                                               |
            // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

            byte[] request = new byte[20]; // Header-only request

            // Set message type to Binding Request
            request[0] = 0x00;
            request[1] = 0x01;

            // Message length (0 as we have no attributes)
            request[2] = 0x00;
            request[3] = 0x00;

            // Magic cookie (fixed value)
            request[4] = 0x21;
            request[5] = 0x12;
            request[6] = 0xA4;
            request[7] = 0x42;

            // Transaction ID (random 12 bytes)
            Random rand = new Random();
            for (int i = 8; i < 20; i++)
            {
                request[i] = (byte)rand.Next(256);
            }

            return request;
        }

        private IPEndPoint ParseStunResponse(byte[] response)
        {
            if (response.Length < 20)
            {
                throw new ArgumentException("STUN response too short");
            }

            // Check message type
            ushort messageType = (ushort)((response[0] << 8) + response[1]);
            if (messageType != BindingResponse)
            {
                throw new ArgumentException($"Unexpected STUN message type: 0x{messageType:X4}");
            }

            // Get message length
            ushort messageLength = (ushort)((response[2] << 8) + response[3]);

            // Check magic cookie
            uint magicCookie = (uint)((response[4] << 24) + (response[5] << 16) + (response[6] << 8) + response[7]);
            if (magicCookie != 0x2112A442)
            {
                throw new ArgumentException("Invalid magic cookie in STUN response");
            }

            // Parse attributes
            int pos = 20; // Start of attributes
            IPAddress mappedAddress = null;
            int mappedPort = 0;

            while (pos + 4 <= response.Length && pos < 20 + messageLength)
            {
                // Attribute header - need at least 4 bytes for type and length
                ushort attrType = (ushort)((response[pos] << 8) + response[pos + 1]);
                ushort attrLength = (ushort)((response[pos + 2] << 8) + response[pos + 3]);
                pos += 4;

                // Verify we have enough data for the attribute value
                if (pos + attrLength > response.Length)
                    break;

                if (attrType == MappedAddress && attrLength >= 8)
                {
                    // Skip first byte (reserved) and family
                    byte family = response[pos + 1];
                    if (family != 1) // IPv4
                    {
                        throw new NotSupportedException("Only IPv4 is supported");
                    }

                    // Port (network byte order)
                    mappedPort = (response[pos + 2] << 8) + response[pos + 3];

                    // Address
                    byte[] ipBytes = new byte[4];
                    Array.Copy(response, pos + 4, ipBytes, 0, 4);
                    mappedAddress = new IPAddress(ipBytes);
                }
                else if (attrType == XorMappedAddress && attrLength >= 8)
                {
                    // Skip first byte (reserved) and family
                    byte family = response[pos + 1];
                    if (family != 1) // IPv4
                    {
                        throw new NotSupportedException("Only IPv4 is supported");
                    }

                    // XOR-mapped port (XOR with first 2 bytes of magic cookie)
                    mappedPort = ((response[pos + 2] << 8) + response[pos + 3]) ^ (0x2112);

                    // XOR-mapped address (XOR with magic cookie)
                    byte[] ipBytes = new byte[4];
                    for (int i = 0; i < 4; i++)
                    {
                        ipBytes[i] = (byte)(response[pos + 4 + i] ^ response[4 + i]);
                    }
                    mappedAddress = new IPAddress(ipBytes);
                }

                // Move to next attribute
                pos += attrLength;

                // Attributes are padded to 4-byte boundaries
                if (attrLength % 4 != 0)
                {
                    pos += 4 - (attrLength % 4);
                }
            }

            if (mappedAddress == null)
            {
                throw new ArgumentException("No mapped address found in STUN response");
            }

            return new IPEndPoint(mappedAddress, mappedPort);
        }
    }
}