#include "cpu.h"
#include "YBaseLib/HashTable.h"

class JitBase : public CPU
{
public:
    JitBase(System *system);
    virtual ~JitBase();

    virtual void ExecuteInstruction() override;

    static JitBase *CreateJitCPU(System *system);

protected:
    struct Block
    {
        uint32 StartVirtualAddress;
        uint16 StartRealAddress;
        uint32 InstructionCount;
        uint32 ByteCount;
    };

    static bool InJittableRange(uint16 real_address);

    uint32 GetVirtualAddress(uint16 address) const;
    uint8 ReadVirtualAddress(uint32 virtual_address);

    bool LookupBlock(uint32 virtual_address, Block **block_ptr);
    Block *CreateBlock(uint32 virtual_address, uint16 real_address);
    bool AnalyseBlock(Block *block);

    virtual Block *AllocateBlock(uint32 virtual_address, uint16 real_address) = 0;
    virtual bool CompileBlock(Block *block) = 0;
    virtual void ExecuteBlock(Block *block) = 0;
    virtual void DestroyBlock(Block *block) = 0;

    HashTable<uint32, Block *> m_blocks;
};
