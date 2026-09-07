// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

// Dumps the Apple Accessory Protocol SDP record that paired AirPods publish, and pulls
// the L2CAP PSM out of it.
//
// AAP is the channel Apple uses for noise control. Windows exposes no user-mode L2CAP for
// BR/EDR, so reaching it eventually needs a profile driver -- but the discovery half of
// that path is reachable from an ordinary process, and this answers whether Windows
// already knows everything such a driver would need, before a line of kernel code exists.
//
// Deliberately outside the CMake build: it shares no headers with the application and is
// a diagnostic, not a component.
//
//     cl /nologo /EHsc /W4 /std:c++17 Tools\AapSdpDump.cpp /link ws2_32.lib bthprops.lib

#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bthsdpdef.h>
#include <bthdef.h>
#include <bluetoothapis.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// {74EC2172-0BAD-4D01-8F77-997B2BE0722A} -- the service AirPods advertise for AAP.
const GUID kAapServiceClass = {
    0x74ec2172, 0x0bad, 0x4d01, {0x8f, 0x77, 0x99, 0x7b, 0x2b, 0xe0, 0x72, 0x2a}};

constexpr USHORT kUuidL2cap = 0x0100;
constexpr USHORT kDocumentedAapPsm = 0x1001;
constexpr ULONG kAttrProtocolDescriptorList = 0x0004;

// rgBytes is little-endian, so the printable form walks it backwards.
std::wstring FormatAddress(const BLUETOOTH_ADDRESS &address)
{
    wchar_t buffer[32];
    swprintf(buffer, ARRAYSIZE(buffer), L"%02X:%02X:%02X:%02X:%02X:%02X", address.rgBytes[5],
             address.rgBytes[4], address.rgBytes[3], address.rgBytes[2], address.rgBytes[1],
             address.rgBytes[0]);
    return buffer;
}

void PrintHex(const BYTE *data, ULONG size)
{
    for (ULONG i = 0; i < size; i += 16) {
        printf("    %04lX  ", i);
        for (ULONG j = 0; j < 16; ++j) {
            if (i + j < size) {
                printf("%02X ", data[i + j]);
            }
            else {
                printf("   ");
            }
        }
        printf(" |");
        for (ULONG j = 0; j < 16 && i + j < size; ++j) {
            const BYTE byte = data[i + j];
            printf("%c", (byte >= 0x20 && byte < 0x7F) ? static_cast<char>(byte) : '.');
        }
        printf("|\n");
    }
}

// A ProtocolDescriptorList is a sequence of per-layer sequences. The L2CAP layer is the
// one whose first element is UUID 0x0100; its next element, when present, is the PSM.
bool ExtractPsm(const BYTE *stream, ULONG size, USHORT *psm)
{
    SDP_ELEMENT_DATA list{};
    if (BluetoothSdpGetElementData(const_cast<LPBYTE>(stream), size, &list) != ERROR_SUCCESS) {
        return false;
    }
    if (list.type != SDP_TYPE_SEQUENCE) {
        return false;
    }

    HBLUETOOTH_CONTAINER_ELEMENT layerIter = nullptr;
    SDP_ELEMENT_DATA layer{};

    while (BluetoothSdpGetContainerElementData(list.data.sequence.value, list.data.sequence.length,
                                              &layerIter, &layer) == ERROR_SUCCESS) {
        if (layer.type != SDP_TYPE_SEQUENCE) {
            continue;
        }

        HBLUETOOTH_CONTAINER_ELEMENT fieldIter = nullptr;
        SDP_ELEMENT_DATA field{};
        bool isL2cap = false;

        while (BluetoothSdpGetContainerElementData(layer.data.sequence.value,
                                                   layer.data.sequence.length, &fieldIter,
                                                   &field) == ERROR_SUCCESS) {
            if (!isL2cap) {
                isL2cap = field.type == SDP_TYPE_UUID && field.specificType == SDP_ST_UUID16 &&
                          field.data.uuid16 == kUuidL2cap;
                continue;
            }
            if (field.type == SDP_TYPE_UINT && field.specificType == SDP_ST_UINT16) {
                *psm = field.data.uint16;
                return true;
            }
        }
    }
    return false;
}

struct RecordContext
{
    USHORT psm = 0;
    bool psmFound = false;
};

BOOL CALLBACK OnAttribute(ULONG id, LPBYTE stream, ULONG size, LPVOID param)
{
    auto *context = static_cast<RecordContext *>(param);

    printf("  attribute 0x%04lX (%lu bytes)\n", id, size);
    PrintHex(stream, size);

    if (id == kAttrProtocolDescriptorList && !context->psmFound) {
        context->psmFound = ExtractPsm(stream, size, &context->psm);
    }
    return TRUE;
}

// Queries one paired device for the AAP service. The cache is used by default on purpose:
// Windows stores the record at pairing time, so it answers with the buds in their case.
bool QueryDevice(const std::wstring &address, bool live)
{
    const std::wstring context = L"(" + address + L")";

    WSAQUERYSETW query{};
    query.dwSize = sizeof(query);
    query.dwNameSpace = NS_BTH;
    query.lpServiceClassId = const_cast<GUID *>(&kAapServiceClass);
    query.lpszContext = const_cast<LPWSTR>(context.c_str());

    DWORD flags = LUP_RETURN_ALL;
    if (live) {
        flags |= LUP_FLUSHCACHE;
    }

    HANDLE lookup = nullptr;
    if (WSALookupServiceBeginW(&query, flags, &lookup) != 0) {
        printf("  no AAP record (WSALookupServiceBegin: %d)\n", WSAGetLastError());
        return false;
    }

    std::vector<BYTE> buffer(8192);
    bool found = false;

    for (;;) {
        DWORD size = static_cast<DWORD>(buffer.size());
        auto *results = reinterpret_cast<WSAQUERYSETW *>(buffer.data());

        if (WSALookupServiceNextW(lookup, flags, &size, results) != 0) {
            const int error = WSAGetLastError();
            if (error == WSAEFAULT && size > buffer.size() && size < (1u << 20)) {
                buffer.resize(size);
                continue;
            }
            if (error != WSA_E_NO_MORE && error != WSAENOMORE) {
                printf("  lookup ended: %d\n", error);
            }
            break;
        }

        if (results->lpBlob == nullptr || results->lpBlob->cbSize == 0) {
            continue;
        }

        found = true;
        printf("  raw SDP record (%lu bytes):\n", results->lpBlob->cbSize);
        PrintHex(results->lpBlob->pBlobData, results->lpBlob->cbSize);

        RecordContext record;
        BluetoothSdpEnumAttributes(results->lpBlob->pBlobData, results->lpBlob->cbSize, OnAttribute,
                                   &record);

        if (record.psmFound) {
            printf("\n  --> L2CAP PSM 0x%04X%s\n", record.psm,
                   record.psm == kDocumentedAapPsm ? "  (matches the documented AAP PSM)" : "");
        }
        else {
            printf("\n  --> no PSM in the record; a driver would have to assume 0x%04X\n",
                   kDocumentedAapPsm);
        }
    }

    WSALookupServiceEnd(lookup);
    return found;
}

} // namespace

int main(int argc, char *argv[])
{
    const bool live = argc > 1 && strcmp(argv[1], "--live") == 0;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnConnected = TRUE;

    BLUETOOTH_DEVICE_INFO device{};
    device.dwSize = sizeof(device);

    HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&search, &device);
    if (find == nullptr) {
        fprintf(stderr, "No paired Bluetooth devices found.\n");
        WSACleanup();
        return 1;
    }

    printf("Querying %s SDP records for the AAP service.\n\n", live ? "live" : "cached");

    int matches = 0;
    do {
        const std::wstring address = FormatAddress(device.Address);
        wprintf(L"%s  [%s]  %s\n", device.szName, address.c_str(),
                device.fConnected ? L"connected" : L"not connected");

        if (QueryDevice(address, live)) {
            ++matches;
        }
        printf("\n");
    } while (BluetoothFindNextDevice(find, &device));

    BluetoothFindDeviceClose(find);
    WSACleanup();

    printf("%d device(s) publish the AAP service.\n", matches);
    return matches > 0 ? 0 : 2;
}
