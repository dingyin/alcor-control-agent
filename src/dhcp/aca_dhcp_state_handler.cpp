// Copyright 2019 The Alcor Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "aca_log.h"
#include "aca_util.h"
#include "aca_dhcp_server.h"
#include "aca_dhcp_state_handler.h"
#include "aca_goal_state_handler.h"
#include "goalstateprovisioner.grpc.pb.h"
#include <future>

using namespace aca_dhcp_programming_if;
using namespace alcor::schema;

namespace aca_dhcp_state_handler
{
Aca_Dhcp_State_Handler::Aca_Dhcp_State_Handler()
{
  ACA_LOG_INFO("DHCP State Handler: using dhcp server\n");
  this->dhcp_programming_if = &(aca_dhcp_server::ACA_Dhcp_Server::get_instance());
}

Aca_Dhcp_State_Handler::~Aca_Dhcp_State_Handler()
{
  // allocated dhcp_programming_if is destroyed when program exits.
}

Aca_Dhcp_State_Handler &Aca_Dhcp_State_Handler::get_instance()
{
  // It is instantiated on first use.
  // allocated instance is destroyed when program exits.
  static Aca_Dhcp_State_Handler instance;
  return instance;
}

int Aca_Dhcp_State_Handler::update_dhcp_state_workitem(const DHCPState current_DhcpState,
                                                       GoalStateOperationReply &gsOperationReply)
{
  dhcp_config stDhcpCfg;
  int overall_rc = EXIT_SUCCESS;
  ulong culminative_dataplane_programming_time = 0;
  ulong culminative_network_configuration_time = 0;

  auto operation_start = chrono::steady_clock::now();

  DHCPConfiguration current_DhcpConfiguration = current_DhcpState.configuration();
  stDhcpCfg.mac_address = current_DhcpConfiguration.mac_address();
  stDhcpCfg.ipv4_address = current_DhcpConfiguration.ipv4_address();
  stDhcpCfg.ipv6_address = current_DhcpConfiguration.ipv6_address();
  stDhcpCfg.port_host_name = current_DhcpConfiguration.port_host_name();

  switch (current_DhcpState.operation_type()) {
  case OperationType::CREATE:
    overall_rc = this->dhcp_programming_if->add_dhcp_entry(&stDhcpCfg);
    break;
  case OperationType::UPDATE:
    overall_rc = this->dhcp_programming_if->update_dhcp_entry(&stDhcpCfg);
    break;
  case OperationType::DELETE:
    overall_rc = this->dhcp_programming_if->delete_dhcp_entry(&stDhcpCfg);
    break;
  default:
    ACA_LOG_ERROR("=====>wrong dhcp operation\n");
    break;
  }

  auto operation_end = chrono::steady_clock::now();

  auto operation_total_time =
          cast_to_nanoseconds(operation_end - operation_start).count();

  aca_goal_state_handler::Aca_Goal_State_Handler::get_instance().add_goal_state_operation_status(
          gsOperationReply, "NA_ID", DHCP, current_DhcpState.operation_type(),
          overall_rc, culminative_dataplane_programming_time,
          culminative_network_configuration_time, operation_total_time);

  return overall_rc;
}

int Aca_Dhcp_State_Handler::update_dhcp_states(GoalState &parsed_struct,
                                               GoalStateOperationReply &gsOperationReply)
{
  std::vector<std::future<int> > workitem_future;
  int rc;
  int overall_rc = EXIT_SUCCESS;

  // if (parsed_struct.dhcp_states_size() == 0)
  //   overall_rc = EXIT_SUCCESS in the current logic

  for (int i = 0; i < parsed_struct.dhcp_states_size(); i++) {
    ACA_LOG_DEBUG("=====>parsing dhcp states #%d\n", i);

    DHCPState current_DhcpState = parsed_struct.dhcp_states(i);

    workitem_future.push_back(std::async(
            std::launch::async, &Aca_Dhcp_State_Handler::update_dhcp_state_workitem,
            this, current_DhcpState, std::ref(gsOperationReply)));

    //workitem_future.push_back(std::async(
    //        std::launch::async, &Aca_Dhcp_State_Handler::update_dhcp_state_workitem, this,
    //        current_DhcpState, std::ref(parsed_struct), std::ref(gsOperationReply)));

    // keeping below just in case if we want to call it serially
    // rc = update_dhcp_state_workitem(current_DHCPState, parsed_struct, gsOperationReply);
    // if (rc != EXIT_SUCCESS)
    //   overall_rc = rc;
  } // for (int i = 0; i < parsed_struct.dhcp_states_size(); i++)

  for (int i = 0; i < parsed_struct.dhcp_states_size(); i++) {
    rc = workitem_future[i].get();
    if (rc != EXIT_SUCCESS)
      overall_rc = rc;
  } // for (int i = 0; i < parsed_struct.port_states_size(); i++)

  return overall_rc;
}

} // namespace aca_dhcp_state_handler
