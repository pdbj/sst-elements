// Copyright 2012-2016 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2012-2016, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef _CPU_H
#define _CPU_H

#include <sst/core/component.h>
#include <sst/elements/VaultSimC/memReqEvent.h>
#include <sst/core/rng/sstrng.h>
#include <sst/core/output.h>


using namespace std;

namespace SST { 
namespace VaultSim {

#ifndef VAULTSIMC_DBG
#define VAULTSIMC_DBG 0
#endif

//#define STUPID_DEBUG 

class cpu : public Component {

public: // functions

  cpu( ComponentId_t id, Params& params );
  void finish();
  
private: // types
  
  typedef SST::Link memChan_t;
  typedef set<uint64_t> memSet_t;
  typedef vector<memSet_t> thrSet_t;
  typedef vector<int> coreVec_t;

private: 
  
  cpu( const cpu& c );
  bool clock( Cycle_t );
  
  SST::RNG::SSTRandom* rng;
  memChan_t *toMem;
  unsigned int outstanding;
  unsigned long long memOps;
  unsigned long long inst;
  int threads;
  int app;
  int bwlimit;
  thrSet_t thrOutstanding;
  coreVec_t coreAddr;

  MemReqEvent *getInst(int cacheLevel, int app, int core);

protected:
  Output &out;
};

}
}

#endif
