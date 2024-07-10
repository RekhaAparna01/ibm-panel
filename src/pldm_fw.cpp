#include "pldm_fw.hpp"

#include "exception.hpp"
#include "utils.hpp"

#include <libpldm/entity.h>
#include <libpldm/platform.h>
#include <libpldm/pldm.h>
#include <libpldm/state_set.h>
#include <poll.h>

#include <iostream>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace panel
{
pldm_instance_id_t PldmFramework::getInstanceID()
{
    // Fetch Instance ID DB
    int rc = pldm_instance_db_init_default(&pldmInstanceDb);

    if (rc != 0)
    {
        throw std::runtime_error(
            "Call to pldm_instance_db_init_default failed with return code " +
            std::to_string(rc));
    }

    // Initialising with an invalid/unused ID.
    pldm_instance_id_t instanceID = constants::PLDM_INST_ID_MAX + 1;

    rc = pldm_instance_id_alloc(pldmInstanceDb, tid, &instanceID);
    if (rc == -EAGAIN)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        rc = pldm_instance_id_alloc(pldmInstanceDb, tid, &instanceID);
    }

    if (rc != 0)
    {
        throw FunctionFailure(
            "Call to pldm_instance_id_alloc failed with return code " +
            std::to_string(rc));
    }

    return instanceID;
}

void PldmFramework::freeInstanceID(pldm_instance_id_t instanceID)
{
    if (!pldmInstanceDb)
    {
        return;
    }

    // constants::PLDM_INST_ID_MAX is 32 (0 to 31)
    if (instanceID < constants::PLDM_INST_ID_MAX)
    {
        int rc = pldm_instance_id_free(pldmInstanceDb, tid, instanceID);

        if (rc == -EAGAIN)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            rc = pldm_instance_id_free(pldmInstanceDb, tid, instanceID);
        }

        if (rc != 0)
        {
            std::cerr << "pldm_instance_id_free failed to free id = "
                      << static_cast<int>(instanceID)
                      << " of tid = " << static_cast<int>(tid) << " rc = " << rc
                      << std::endl;
        }
    }

    // Destroy the instance ID DB
    int rc = pldm_instance_db_destroy(pldmInstanceDb);
    if (rc != 0)
    {
        std::cerr << "pldm_instance_db_destroy failed with rc, " << rc << "\n";
    }

    pldmInstanceDb = nullptr;
}

int PldmFramework::openTransport()
{
    if (pldmTransport)
    {
        throw FunctionFailure("pldmTransport is already opened. Requested "
                              "action is not allowed.");
    }

    int rc = pldm_transport_mctp_demux_init(&mctpSocket);
    if (rc != 0)
    {
        std::cerr << "Call to pldm_transport_mctp_demux_init failed with rc = "
                  << rc << std::endl;
        throw FunctionFailure("Failed to init MCTP demux transport");
    }

    rc = pldm_transport_mctp_demux_map_tid(mctpSocket, tid, tid);
    if (rc != 0)
    {
        std::cerr
            << "Call to pldm_transport_mctp_demux_map_tid failed with rc = "
            << rc << std::endl;
        throw FunctionFailure(
            "Call to pldm_transport_mctp_demux_map_tid failed with rc = " +
            std::to_string(rc));
    }

    pldmTransport = pldm_transport_mctp_demux_core(mctpSocket);

    if (!pldmTransport)
    {
        throw FunctionFailure(
            "openTransport: Failed to get pldm_transport object.");
    }

    struct pollfd pollfd;
    rc = pldm_transport_mctp_demux_init_pollfd(pldmTransport, &pollfd);
    if (rc != 0)
    {
        std::cerr << "openTransport: Failed to get pollfd. rc = " << rc
                  << std::endl;
        throw FunctionFailure("openTransport: Failed to get pollfd. rc = " +
                              std::to_string(rc));
    }
    return pollfd.fd;
}

void PldmFramework::closeTransport()
{
    if (mctpSocket)
    {
        // Deallocate memory pointed by pldmTransport object
        pldm_transport_mctp_demux_destroy(mctpSocket);
        mctpSocket = nullptr;
    }

    if (pldmTransport)
    {
        pldmTransport = nullptr;
    }
}

void PldmFramework::fetchPanelEffecterStateSet(const types::PdrList& pdrs,
                                               uint16_t& effecterId,
                                               types::Byte& effecterCount,
                                               types::Byte& panelEffecterPos)
{
    auto pdr =
        reinterpret_cast<const pldm_state_effecter_pdr*>(pdrs.front().data());

    auto possibleStates =
        reinterpret_cast<const state_effecter_possible_states*>(
            pdr->possible_states);

    for (auto offset = 0; offset < pdr->composite_effecter_count; offset++)
    {
        if (possibleStates->state_set_id == stateIdToEnablePanelFunc)
        {
            effecterId = pdr->effecter_id;
            effecterCount = pdr->composite_effecter_count;
            panelEffecterPos = offset;
            return;
        }
    }

    throw FunctionFailure(
        "State set ID to enable panel function could not be found in PDR.");
}

types::PldmPacket
    PldmFramework::prepareSetEffecterReq(const types::PdrList& pdrs,
                                         types::Byte instanceId,
                                         const types::FunctionNumber& function)
{
    types::Byte effecterCount = 0;
    types::Byte panelEffecterPos = 0;
    uint16_t effecterId = 0;

    fetchPanelEffecterStateSet(pdrs, effecterId, effecterCount,
                               panelEffecterPos);

    types::PldmPacket request(
        sizeof(pldm_msg_hdr) + sizeof(effecterId) + sizeof(effecterCount) +
        (effecterCount * sizeof(set_effecter_state_field)));

    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());

    std::vector<set_effecter_state_field> stateField;

    for (types::Byte pos = 0; panelEffecterPos <= effecterCount; pos++)
    {
        if (pos == panelEffecterPos)
        {
            stateField.emplace_back(
                set_effecter_state_field{PLDM_REQUEST_SET, function});
            break;
        }
        else
        {
            stateField.emplace_back(
                set_effecter_state_field{PLDM_NO_CHANGE, 0});
        }
    }

    int rc = encode_set_state_effecter_states_req(
        instanceId, effecterId, effecterCount, stateField.data(), requestMsg);

    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "Return code = " << rc << std::endl;
        throw FunctionFailure(
            "pldm: encode set effecter states request returned error.");
    }
    return request;
}

void PldmFramework::sendPanelFunctionToPhyp(
    const types::FunctionNumber& funcNumber)
{
    // Initialising with invalid/unused ID
    pldm_instance_id_t instance = constants::PLDM_INST_ID_MAX + 1;

    try
    {
        // Get PDR for Panel
        types::PdrList pdrs =
            utils::getPDR(phypTerminusID, frontPanelBoardEntityId,
                          stateIdToEnablePanelFunc, "FindStateEffecterPDR");

        if (pdrs.empty())
        {
            throw std::runtime_error(
                "Empty PDR returned for front panel board entity.");
        }

        // Allocate instance ID
        instance = getInstanceID();

        // Prepare the message packet
        types::PldmPacket packet =
            prepareSetEffecterReq(pdrs, instance, funcNumber);

        if (packet.empty())
        {
            throw std::runtime_error(
                "pldm:SetStateEffecterStates request message empty");
        }

        // Open PLDM socket based communication
        int fd = openTransport();

        pldm_tid_t pldmTID = static_cast<pldm_tid_t>(hostEid);
        auto rc = pldm_transport_send_msg(pldmTransport, pldmTID, packet.data(),
                                          packet.size());

        std::cout << "Panel function " << static_cast<int>(funcNumber)
                  << ". Data packet sent to pldm: ";
        for (const auto i : packet)
        {
            std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)i
                      << " ";
        }
        std::cout << std::endl;

        if (rc != pldm_requester_rc_t::PLDM_REQUESTER_SUCCESS)
        {
            throw std::runtime_error(
                "pldm: pldm_transport_send_msg failed for panel function " +
                static_cast<int>(funcNumber) + std::string(". Return code = ") +
                std::to_string(rc) + std::string(". File descriptor = ") +
                std::to_string(fd) + std::string(". Errno = ") +
                std::to_string(errno));
        }
        std::cout << "Panel function " << static_cast<int>(funcNumber)
                  << " executed successfully." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::map<std::string, std::string> additionalData{};
        additionalData.emplace("DESCRIPTION",
                               "panel to host communication failed. Error: " +
                                   std::string(e.what()));
        additionalData.emplace("ERRNO:", strerror(errno));
        utils::createPEL("com.ibm.Panel.Error.HostCommunicationError",
                         "xyz.openbmc_project.Logging.Entry.Level.Warning",
                         additionalData);
    }

    closeTransport();
    freeInstanceID(instance);
}
} // namespace panel
