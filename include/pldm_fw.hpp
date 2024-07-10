#include "types.hpp"

#include <libpldm/instance-id.h>
#include <libpldm/transport.h>
#include <libpldm/transport/mctp-demux.h>
#include <stdint.h>

#include <vector>

namespace panel
{
/**
 * @brief A class to implement Pldm related functionalities.
 */
class PldmFramework
{
  public:
    /* Deleted methods */
    PldmFramework(const PldmFramework&) = delete;
    PldmFramework& operator=(const PldmFramework&) = delete;
    PldmFramework(PldmFramework&&) = delete;
    PldmFramework& operator=(const PldmFramework&&) = delete;

    /**
     * @brief Construtor
     */
    PldmFramework() = default;

    /**
     * @brief Destructor
     */
    ~PldmFramework() = default;

    /**
     * @brief Send Panel Function to PHYP.
     * This api is used to send panel function number to phyp by fetching and
     * setting the corresponding effector.
     *
     * @param[in] funcNumber - Function number that needs to be sent to PHYP.
     */
    void sendPanelFunctionToPhyp(const types::FunctionNumber& funcNumber);

  private:
    // TODO: <https://github.com/ibm-openbmc/ibm-panel/issues/57>
    // use PLDM defined header file to refer following constants.
    /** Host mctp eid */
    static constexpr auto hostEid = (types::Byte)9;

    /** Terminus ID to which the packet has to be sent */
    static constexpr types::TerminusID tid = hostEid;

    /** PLDM instance database object used to get instance IDs */
    pldm_instance_db* pldmInstanceDb = nullptr;

    /** pldm transport instance  */
    pldm_transport* pldmTransport = nullptr;

    /** PLDM MCTP demux object which is required to establish MCTP socket based
     * communication. */
    pldm_transport_mctp_demux* mctpSocket = nullptr;

    // Constants required for PLDM packet.
    static constexpr auto phypTerminusID = (types::Byte)208;
    static constexpr auto frontPanelBoardEntityId = (uint16_t)32837;
    static constexpr auto stateIdToEnablePanelFunc = (uint16_t)32778;

    /**
     * @brief An api to prepare "set effecter" request packet.
     * This api prepares the message packet that needs to be sent to the PHYP.
     *
     * @param[in] pdrs - Panel pdr data.
     * @param[in] instanceId - instance id which uniquely identifies the
     * requested message packet. This needs to be encoded in the message packet.
     * @param[in] function - function number that needs to be sent to PHYP.
     *
     * @return Returns a Pldm packet.
     */
    types::PldmPacket
        prepareSetEffecterReq(const types::PdrList& pdrs,
                              types::Byte instanceId,
                              const types::FunctionNumber& function);

    /**
     * @brief Fetch the Panel effecter state set from PDR.
     * This api fetches host effecter id, effecter count and effecter position
     * from the panel's PDR.
     * @param[in] pdrs - Panel PDR data.
     * @param[out] effecterId - effecter id retrieved from the PDR.
     * @param[out] effecterCount - effecter count retrieved from the PDR.
     * @param[out] panelEffecterPos - Position of panel effecter.
     */

    void fetchPanelEffecterStateSet(const types::PdrList& pdrs,
                                    uint16_t& effecterId,
                                    types::Byte& effecterCount,
                                    types::Byte& panelEffecterPos);

    /**
     * @brief Get instance ID
     * Instance id is to uniquely identify a message packet. This api fetches
     * the instance ID data base from libpldm and calls libpldm to allocate the
     * valid instance ID.
     *
     * @return pldm_instance_id_t instance id.
     *
     * @throw Exception when failed to allocate instance ID.
     */
    pldm_instance_id_t getInstanceID();

    /**
     * @brief Free PLDM requester instance id
     * This API frees up valid instance ID along side it takes care of
     * destroying the instance ID data base as well.
     *
     * @param[in] instanceID - Instance ID.
     */
    void freeInstanceID(pldm_instance_id_t instanceID);

    /**
     * @brief Opens the socket for sending and receiving messages.
     *
     * @return file descriptor of socket established by PLDM for communication
     * between BMC and host.
     *
     * @throw Exception when failed to get file descriptor.
     */
    int openTransport();

    /** @brief Close the PLDM transport connection*/
    void closeTransport();
};
} // namespace panel
