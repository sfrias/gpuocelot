/*! \file BranchDivergenceInstrumentor.cpp
	\date Monday July 18, 2011
	\author Naila Farooqui <naila@cc.gatech.edu>
	\brief The source file for the BranchDivergenceInstrumentor class.
*/

#ifndef BRANCH_DIVERGENCE_INSTRUMENTOR_CPP_INCLUDED
#define BRANCH_DIVERGENCE_INSTRUMENTOR_CPP_INCLUDED

#include <ocelot/analysis/interface/BranchDivergenceInstrumentor.h>

#include <ocelot/cuda/interface/cuda_runtime.h>

#include <ocelot/transforms/interface/CToPTXInstrumentationPass.h>
#include <ocelot/ir/interface/Module.h>

#include <hydrazine/implementation/ArgumentParser.h>
#include <hydrazine/implementation/string.h>
#include <hydrazine/implementation/debug.h>
#include <hydrazine/implementation/Exception.h>
#include <hydrazine/implementation/json.h>

#include <fstream>
#include <math.h>

using namespace hydrazine;

namespace analysis
{

    void BranchDivergenceInstrumentor::checkConditions() {
        conditionsMet = true;
    }

    void BranchDivergenceInstrumentor::analyze(ir::Module &module) {
        
        warpCount = 0;
        
        struct cudaDeviceProp properties;
        cudaGetDeviceProperties(&properties, 0);
        
        warpCount = (unsigned long)ceil((threads/threadBlocks)/properties.warpSize);
        if(warpCount == 0)
            warpCount = 1;
        for (ir::Module::KernelMap::const_iterator kernel = module.kernels().begin(); 
	        kernel != module.kernels().end(); ++kernel) 
	    {
	        totalBranchesMap[kernel->first] = 0;
	        conditionalBranchesMap[kernel->first] = 0;
	        
            for( ir::ControlFlowGraph::const_iterator block = kernel->second->cfg()->begin(); 
			    block != kernel->second->cfg()->end(); ++block ) {
                
                for( ir::ControlFlowGraph::InstructionList::const_iterator instruction = block->instructions.begin();
                    instruction != block->instructions.end(); ++instruction)
                {
                    ir::PTXInstruction *ptxInst = (ir::PTXInstruction *)*instruction;
                
                    if(ptxInst->opcode == ir::PTXInstruction::Bra)
                    {
                        totalBranchesMap[kernel->first]++;
                        
                        if(ptxInst->pg.condition == ir::PTXOperand::Pred || ptxInst->pg.condition == ir::PTXOperand::InvPred)
                            conditionalBranchesMap[kernel->first]++;
                    }
                }
            } 
        }     
    }

    void BranchDivergenceInstrumentor::initialize() {
        branchDivInfo = 0;

        if(conditionalBranchesMap[kernelName] == 0 || warpCount == 0)
            return;

        cudaMalloc((void **) &branchDivInfo, conditionalBranchesMap[kernelName] * warpCount * sizeof(size_t));
        cudaMemset( branchDivInfo, 0, conditionalBranchesMap[kernelName] * warpCount * sizeof( size_t ));
    
        cudaMemcpyToSymbol(symbol.c_str(), &branchDivInfo, sizeof(size_t *), 0, cudaMemcpyHostToDevice);   
    }

    void BranchDivergenceInstrumentor::createPasses() 
    {
        transforms::CToPTXInstrumentationPass *pass = new transforms::CToPTXInstrumentationPass("resources/branchDivergence.c");
        symbol = pass->baseAddress;
        passes[0] = pass;
    }

    void BranchDivergenceInstrumentor::extractResults(std::ostream *out) {
            
        if(conditionalBranchesMap[kernelName] == 0 || warpCount == 0)
        {
            std::cout << "No conditional branches in this kernel.\n";    
            return;
        }    
        
        size_t conditionalBranches = conditionalBranchesMap[kernelName];
        size_t *info = new size_t[conditionalBranches * warpCount];
        
        if(branchDivInfo) {
            cudaMemcpy(info, branchDivInfo, conditionalBranches * warpCount * sizeof( size_t ), cudaMemcpyDeviceToHost);      
            cudaFree(branchDivInfo);
        }

        struct cudaDeviceProp properties;
        cudaGetDeviceProperties(&properties, 0);
        
        unsigned long dynamicDivergentBranches = 0;
        
        for(size_t i = 0; i < conditionalBranches; i++) {
            for(size_t j = 0; j < warpCount; j++) {
                if(info[warpCount * i + j] == 1) {
                    dynamicDivergentBranches++;
                }      
            }
        } 

        
    
        switch(fmt) {
    
            case json:

                
            
            break;
            
            case text:   

                *out << "Kernel Name: " << kernelName << "\n";
                *out << "Thread Block Count: " << threadBlocks << "\n";
                *out << "Thread Count: " << threads << "\n";
                
                *out << "\n% Branch Divergence: " << dynamicDivergentBranches << "/" << (warpCount * totalBranchesMap[kernelName]) << "\n\n"; 
            
            break;

        }

        if(info)
            delete[] info;
            
    }

    BranchDivergenceInstrumentor::BranchDivergenceInstrumentor() : description("Branch Divergence") {

    }

}

#endif
