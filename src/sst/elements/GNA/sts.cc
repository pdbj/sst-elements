// Copyright 2018-2021 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2018-2021, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include "sts.h"
#include "GNA.h"

using namespace SST;
using namespace SST::GNAComponent;

void STS::assign(int neuronNum) {
    const neuron *spiker = myGNA->getNeuron(neuronNum);
    numSpikes = spiker->getWMLLen();
    uint64_t listAddr = spiker->getWMLAddr();

    // for each link, request the WML structure
    for (int i = 0; i < numSpikes; ++i) {
        // AFR: should throttle
        using namespace Interfaces;
        using namespace White_Matter_Types;
        StandardMem::Read *req =
            new StandardMem::Read(listAddr, sizeof(T_Wme));
        myGNA->readMem(req, this);
        listAddr += sizeof(T_Wme);
    }
}

bool STS::isFree() {
    return (numSpikes == 0);
}

void STS::advance(uint now) {
    // AFR: should throttle
    while (incomingReqs.empty() == false) {
        // get the request
        SST::Interfaces::StandardMem::Request *req = incomingReqs.front();

        SST::Interfaces::StandardMem::ReadResp* resp = dynamic_cast<SST::Interfaces::StandardMem::ReadResp*>(req);
        assert(resp);

        // deliver the spike
        auto &data = resp->data;
        uint16_t strength = (resp->data[0]<<8) + resp->data[1];
        uint16_t tempOffset = (data[2]<<8) + data[3];
        uint16_t target = (data[4]<<8) + data[5];
        //printf("  gna deliver str%u to %u @ %u\n", strength, target, tempOffset+now);
        myGNA->deliver(strength, target, tempOffset+now);
        numSpikes--;

        incomingReqs.pop();
        delete req;
    }
}

