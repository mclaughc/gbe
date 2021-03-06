#include "cartridge.h"
#include "YBaseLib/BinaryReadBuffer.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryWriteBuffer.h"
#include "YBaseLib/BinaryWriter.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/CRC32.h"
#include "YBaseLib/Error.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/String.h"
#include "YBaseLib/StringConverter.h"
#include "structures.h"
#include "system.h"
Log_SetChannel(Cartridge);

// http://bgb.bircd.org/pandocs.htm#thecartridgeheader
static const CartridgeTypeInfo CART_TYPEINFOS[] = {
  // id       mbc             ram     battery     timer   rumble
  {0x00, MBC_NONE, false, false, false, false, "ROM ONLY"},             // 00h  ROM ONLY
  {0x01, MBC_MBC1, false, false, false, false, "MBC1"},                 // 01h  MBC1
  {0x02, MBC_MBC1, true, false, false, false, "MBC1+RAM"},              // 02h  MBC1+RAM
  {0x03, MBC_MBC1, true, true, false, false, "MBC1+RAM+BATTERY"},       // 03h  MBC1+RAM+BATTERY
  {0x05, MBC_MBC2, false, false, false, false, "MBC2"},                 // 05h  MBC2
  {0x06, MBC_MBC2, false, true, false, false, "MBC2+BATTERY"},          // 06h  MBC2+BATTERY
  {0x08, MBC_NONE, true, false, false, false, "ROM+RAM"},               // 08h  ROM+RAM
  {0x09, MBC_NONE, true, true, false, false, "ROM+RAM+BATTERY"},        // 09h  ROM+RAM+BATTERY
  {0x0B, MBC_MMM01, false, false, false, false, "MMM01"},               // 0Bh  MMM01
  {0x0C, MBC_MMM01, true, false, false, false, "MMM01+RAM"},            // 0Ch  MMM01+RAM
  {0x0D, MBC_MMM01, true, true, false, false, "MMM01+RAM+BATTERY"},     // 0Dh  MMM01+RAM+BATTERY
  {0x0F, MBC_MBC3, false, true, true, false, "MBC3+TIMER+BATTERY"},     // 0Fh  MBC3+TIMER+BATTERY
  {0x10, MBC_MBC3, true, true, true, false, "MBC3+TIMER+RAM+BATTERY"},  // 10h  MBC3+TIMER+RAM+BATTERY
  {0x11, MBC_MBC3, false, false, false, false, "MBC3"},                 // 11h  MBC3
  {0x12, MBC_MBC3, true, false, false, false, "MBC3+RAM"},              // 12h  MBC3+RAM
  {0x13, MBC_MBC3, true, true, false, false, "MBC3+RAM+BATTERY"},       // 13h  MBC3+RAM+BATTERY
  {0x15, MBC_MBC4, false, false, false, false, "MBC4"},                 // 15h  MBC4
  {0x16, MBC_MBC4, true, false, false, false, "MBC4+RAM"},              // 16h  MBC4+RAM
  {0x17, MBC_MBC4, true, true, false, false, "MBC4+RAM+BATTERY"},       // 17h  MBC4+RAM+BATTERY
  {0x19, MBC_MBC5, false, false, false, false, "MBC5"},                 // 19h  MBC5
  {0x1A, MBC_MBC5, true, false, false, false, "MBC5+RAM"},              // 1Ah  MBC5+RAM
  {0x1B, MBC_MBC5, true, true, false, false, "MBC5+RAM+BATTERY"},       // 1Bh  MBC5+RAM+BATTERY
  {0x1C, MBC_MBC5, false, false, false, true, "MBC5+RUMBLE"},           // 1Ch  MBC5+RUMBLE
  {0x1D, MBC_MBC5, true, false, false, true, "MBC5+RUMBLE+RAM"},        // 1Dh  MBC5+RUMBLE+RAM
  {0x1E, MBC_MBC5, true, true, false, true, "MBC5+RUMBLE+RAM+BATTERY"}, // 1Eh  MBC5+RUMBLE+RAM+BATTERY
};
static const char* MBC_NAME_STRINGS[NUM_MBC_TYPES] = {
  "MBC_NONE", "MBC_MBC1", "MBC_MBC2", "MBC_MBC3", "MBC_MBC4", "MBC_MBC5", "MBC_MMM01",
};
static const uint32 CART_EXTERNAL_RAM_SIZES[6] = {
  0, 2048 /* 2KB */, 8192 /* 8KB */, 32768 /* 32KB */, 65536 /* 64KB */, 131072 /* 128KB */};
static const uint32 CART_ROM_BANK_COUNT[][2] = {
  {0x00, 2}, // no rom banks
  {0x01, 4},   {0x02, 8},   {0x03, 16}, {0x04, 32}, {0x05, 64},
  {0x06, 128}, {0x07, 256}, {0x52, 72}, {0x53, 80}, {0x54, 96},
};

Cartridge::Cartridge(System* system)
  : m_system(system), m_mbc(NUM_MBC_TYPES), m_crc(0), m_typeinfo(nullptr), m_rom_banks(nullptr), m_num_rom_banks(0),
    m_external_ram(nullptr), m_external_ram_size(0), m_external_ram_modified(false)
{
  Y_memzero(&m_mbc_data, sizeof(m_mbc_data));
}

Cartridge::~Cartridge()
{
  for (uint32 i = 0; i < m_num_rom_banks; i++)
    Y_free(m_rom_banks[i]);
  delete[] m_rom_banks;
}

bool Cartridge::ParseHeader(ByteStream* pStream, Error* pError)
{
  SmallString str;
  CART_HEADER header;
  if (!pStream->SeekAbsolute(CART_HEADER_OFFSET) || !pStream->Read2(&header, sizeof(header)))
  {
    pError->SetErrorUser(1, "Failed to read cartridge header");
    return false;
  }

  Log_InfoPrint("Cartridge info: ");

  str.Clear();
  str.AppendString(header.title, sizeof(header.title));
  Log_InfoPrintf("  Title: %s", str.GetCharArray());

  str.Clear();
  str.AppendString(header.cgb_title, sizeof(header.cgb_title));
  Log_InfoPrintf("  CGB Title: %s", str.GetCharArray());

  str.Clear();
  str.AppendString(header.cgb_manufacturer, sizeof(header.cgb_manufacturer));
  Log_InfoPrintf("  CGB Manufacturer: %s", str.GetCharArray());

  Log_InfoPrintf("  CGB Flag: 0x%02X", header.cgb_flag);
  Log_InfoPrintf("  CGB Licensee code: %c%c", header.cgb_licensee_code[0], header.cgb_licensee_code[1]);
  Log_InfoPrintf("  SGB Flag: 0x%02X", header.sgb_flag);
  Log_InfoPrintf("  Type: 0x%02X", header.type);
  Log_InfoPrintf("  ROM Size Code: 0x%02X", header.rom_size);
  Log_InfoPrintf("  RAM Size Code: 0x%02X", header.ram_size);
  Log_InfoPrintf("  Region Code: 0x%02X", header.region_code);
  Log_InfoPrintf("  Licensee Code: 0x%02X", header.licensee_code);
  Log_InfoPrintf("  ROM Version: 0x%02X", header.rom_version);
  Log_InfoPrintf("  Header Checksum: 0x%02X", header.header_checksum);
  Log_InfoPrintf("  Cartridge Checksum: 0x%04X", header.cartridge_checksum);

  // set name
  m_name.Clear();
  if ((header.cgb_flag & 0x80) || (header.cgb_flag & 0xC0))
    m_name.AppendString(header.cgb_title, sizeof(header.cgb_title));
  else
    m_name.AppendString(header.title, sizeof(header.title));
  m_name.UpdateSize();

  // get info
  for (uint32 i = 0; i < countof(CART_TYPEINFOS); i++)
  {
    if (CART_TYPEINFOS[i].id == header.type)
    {
      m_typeinfo = &CART_TYPEINFOS[i];
      break;
    }
  }
  if (m_typeinfo == nullptr)
  {
    pError->SetErrorUserFormatted(1, "Unknown cartridge type: 0x%02X", header.type);
    return false;
  }

  // dump cart type info
  Log_InfoPrintf("  Cartridge type description: %s", m_typeinfo->description);
  Log_InfoPrintf("    ID: 0x%02X", m_typeinfo->id);
  Log_InfoPrintf("    Memory bank controller: %s", MBC_NAME_STRINGS[m_typeinfo->mbc]);
  Log_InfoPrintf("    External RAM: %s", m_typeinfo->ram ? "yes" : "no");
  Log_InfoPrintf("    Battery: %s", m_typeinfo->battery ? "yes" : "no");
  Log_InfoPrintf("    Timer: %s", m_typeinfo->timer ? "yes" : "no");
  Log_InfoPrintf("    Rumble: %s", m_typeinfo->rumble ? "yes" : "no");
  m_mbc = m_typeinfo->mbc;

  // parse rom banks
  m_num_rom_banks = 0;
  for (uint32 i = 0; i < countof(CART_ROM_BANK_COUNT); i++)
  {
    if (CART_ROM_BANK_COUNT[i][0] == header.rom_size)
    {
      m_num_rom_banks = CART_ROM_BANK_COUNT[i][1];
      break;
    }
  }
  if (m_num_rom_banks == 0)
  {
    pError->SetErrorUserFormatted(1, "Unknown rom size code: 0x%02X", header.rom_size);
    return false;
  }
  Log_InfoPrintf("  ROM Banks: %u (%s)", m_num_rom_banks,
                 StringConverter::SizeToHumanReadableString(ROM_BANK_SIZE * m_num_rom_banks).GetCharArray());

  // parse ram
  if (header.ram_size >= countof(CART_EXTERNAL_RAM_SIZES) || (header.ram_size > 0 && !m_typeinfo->ram))
  {
    pError->SetErrorUserFormatted(1, "Unknown ram size code: %02X", header.ram_size);
    return false;
  }
  m_external_ram_size = CART_EXTERNAL_RAM_SIZES[header.ram_size];
  Log_InfoPrintf("  External ram size: %s",
                 StringConverter::SizeToHumanReadableString(m_external_ram_size).GetCharArray());

  // choose system mode
  m_system_mode = SYSTEM_MODE_DMG;
  if (header.cgb_flag & 0x80)
    m_system_mode = SYSTEM_MODE_CGB;
  // else if (header.sgb_flag != 0x03)
  // m_system_mode = SYSTEM_MODE_SGB;
  Log_InfoPrintf("  Detected system mode: %s", NameTable_GetNameString(NameTables::SystemMode, m_system_mode));

  //     // MBC2 mapper provides 512 bytes of 4-bit memory
  //     if (m_mbc == MBC_MBC2)
  //         m_external_ram_size = 512;

  size_t extra_bytes = static_cast<size_t>(pStream->GetSize());
  DebugAssert(extra_bytes >= (ROM_BANK_SIZE * m_num_rom_banks));
  if (extra_bytes > (ROM_BANK_SIZE * m_num_rom_banks))
  {
    Log_WarningPrintf("  ROM has %u extra bytes at end of bank space", extra_bytes);
    if (m_mbc != MBC_NONE)
    {
      m_num_rom_banks = extra_bytes / ROM_BANK_SIZE;
      Log_WarningPrintf("    Recalculated ROM banks: %u", m_num_rom_banks);
    }
  }

  return true;
}

bool Cartridge::Load(ByteStream* pStream, Error* pError)
{
  // seek to start, and hash the cart file
  CRC32 crc32;
  if (!pStream->SeekAbsolute(0) || !crc32.HashStream(pStream))
  {
    pError->SetErrorUser(1, "Failed to hash file");
    return false;
  }
  m_crc = crc32.GetCRC();

  // parse header
  if (!pStream->SeekAbsolute(0) || !ParseHeader(pStream, pError))
    return false;

  // back to start - cart header is part of the first rom bank
  if (!pStream->SeekAbsolute(0))
  {
    pError->SetErrorUser(1, "Failed to re-seek");
    return false;
  }

  // read rom banks
  DebugAssert(m_num_rom_banks > 0);
  m_rom_banks = new byte*[m_num_rom_banks];
  for (uint32 i = 0; i < m_num_rom_banks; i++)
  {
    m_rom_banks[i] = (byte*)Y_malloc(ROM_BANK_SIZE);
    DebugAssert(m_rom_banks[i] != nullptr);
    if (!pStream->Read2(m_rom_banks[i], ROM_BANK_SIZE))
    {
      pError->SetErrorUserFormatted(1, "Failed to read ROM bank %u", i);
      return false;
    }
  }

  // handle mappers
  bool mbc_init_result;
  switch (m_mbc)
  {
  case MBC_NONE:
    mbc_init_result = MBC_NONE_Init();
    break;

  case MBC_MBC1:
    mbc_init_result = MBC_MBC1_Init();
    break;

  case MBC_MBC3:
    mbc_init_result = MBC_MBC3_Init();
    break;

  case MBC_MBC5:
    mbc_init_result = MBC_MBC5_Init();
    break;

  default:
    pError->SetErrorUserFormatted(1, "MBC %s not implemented", MBC_NAME_STRINGS[m_mbc]);
    return false;
  }

  // errors
  if (!mbc_init_result)
  {
    pError->SetErrorUserFormatted(1, "MBC %s failed initialization", MBC_NAME_STRINGS[m_mbc]);
    return false;
  }

  // load sram/rtc
  LoadRAM();
  LoadRTC();
  return true;
}

void Cartridge::LoadRAM()
{
  // if no battery, we assume the contents is lost at power-down
  if (m_external_ram_size == 0 || !m_typeinfo->battery)
    return;

  if (!m_system->m_callbacks->LoadCartridgeRAM(m_external_ram, m_external_ram_size))
  {
    Log_WarningPrintf("Failed to load external SRAM, blanking.");
    Y_memzero(m_external_ram, m_external_ram_size);
  }
}

void Cartridge::SaveRAM()
{
  if (m_external_ram_size > 0 && m_typeinfo->battery)
    m_system->m_callbacks->SaveCartridgeRAM(m_external_ram, m_external_ram_size);

  m_external_ram_modified = false;
}

void Cartridge::LoadRTC()
{
  Y_memzero(&m_rtc_data, sizeof(m_rtc_data));
  if (!m_typeinfo->timer)
    return;

  // set defaults
  m_rtc_data.base_time = Timestamp::Now().AsUnixTimestamp();
  m_rtc_data.active = false;

  // load data
  BinaryReadBuffer buffer(16);
  if (m_system->m_callbacks->LoadCartridgeRTC(buffer.GetBufferPointer(), buffer.GetBufferSize()))
  {
    // read buffer
    m_rtc_data.base_time = buffer.ReadUInt64();
    m_rtc_data.offset_seconds = buffer.ReadUInt8();
    m_rtc_data.offset_minutes = buffer.ReadUInt8();
    m_rtc_data.offset_hours = buffer.ReadUInt8();
    m_rtc_data.offset_days = buffer.ReadUInt16();
    m_rtc_data.active = buffer.ReadBool();
  }
  else
  {
    // new file - save the rtc state
    SaveRTC();
  }
}

void Cartridge::SaveRTC()
{
  if (!m_typeinfo->timer)
    return;

  // save offset
  BinaryWriteBuffer buffer;
  buffer.WriteUInt64(m_rtc_data.base_time);
  buffer.WriteUInt16(m_rtc_data.offset_days);
  buffer.WriteUInt8(m_rtc_data.offset_hours);
  buffer.WriteUInt8(m_rtc_data.offset_minutes);
  buffer.WriteUInt8(m_rtc_data.offset_seconds);
  buffer.WriteUInt8(0);
  buffer.WriteUInt8(0);
  buffer.WriteUInt8(0);
  m_system->m_callbacks->SaveCartridgeRTC(buffer.GetBufferPointer(), (size_t)buffer.GetStreamPosition());
}

Cartridge::RTCValue Cartridge::GetCurrentRTCTime() const
{
  Timestamp current_time = Timestamp::Now();
  uint64 current_time_unix = current_time.AsUnixTimestamp();

#if 0
    // apply offset
    (m_rtc_data.offset_seconds >= 0) ? (current_time_unix += uint64(m_rtc_data.offset_seconds)) : (current_time_unix -= uint64(-m_rtc_data.offset_seconds));
    (m_rtc_data.offset_minutes >= 0) ? (current_time_unix += uint64(m_rtc_data.offset_minutes * 60)) : (current_time_unix -= uint64(-m_rtc_data.offset_minutes * 60));
    (m_rtc_data.offset_hours >= 0) ? (current_time_unix += uint64(m_rtc_data.offset_hours * 3600)) : (current_time_unix -= uint64(-m_rtc_data.offset_hours * 3600));
    (m_rtc_data.offset_days >= 0) ? (current_time_unix += uint64(m_rtc_data.offset_days * 86400)) : (current_time_unix -= uint64(-m_rtc_data.offset_days * 86400));

    // apply offset
    if (m_rtc_data.use_offset)
    {
        uint64 diff_time = current_time_unix - m_rtc_data.base_time;
        diff_time += uint64(m_rtc_data.offset_seconds);
        diff_time += uint64(m_rtc_data.offset_minutes * 60);
        diff_time += uint64(m_rtc_data.offset_hours * 3600);
        diff_time += uint64(m_rtc_data.offset_days * 86400);

        // convert to format
        out_value.seconds = uint32(diff_time % 60);
        out_value.minutes = uint32((diff_time / 60) % 60);
        out_value.hours = uint32((diff_time / 3600) % 24);
        out_value.days = uint32((diff_time / 86400));
    }
    else
    {
        Timestamp::ExpandedTime base_time_expanded = Timestamp::FromUnixTimestamp(m_rtc_data.base_time).AsExpandedTime();
        Timestamp::ExpandedTime current_time_expanded = current_time.AsExpandedTime();

        // return the current time
        out_value.seconds = current_time_expanded.Second;
        out_value.minutes = current_time_expanded.Minute;
        out_value.hours = current_time_expanded.Hour;
        out_value.days = base_time_expanded.DayOfWeek + uint32((current_time_unix - m_rtc_data.base_time) / 86400);
    }

    return out_value;

#endif

  uint64 diff_time = current_time_unix - m_rtc_data.base_time;
  diff_time += uint64(m_rtc_data.offset_seconds);
  diff_time += uint64(m_rtc_data.offset_minutes * 60);
  diff_time += uint64(m_rtc_data.offset_hours * 3600);
  diff_time += uint64(m_rtc_data.offset_days * 86400);

  // convert to format
  RTCValue out_value;
  out_value.seconds = uint32(diff_time % 60);
  out_value.minutes = uint32((diff_time / 60) % 60);
  out_value.hours = uint32((diff_time / 3600) % 24);
  out_value.days = uint32((diff_time / 86400));
  return out_value;
}

void Cartridge::Reset()
{
  switch (m_mbc)
  {
  case MBC_NONE:
    MBC_NONE_Reset();
    break;
  case MBC_MBC1:
    MBC_MBC1_Reset();
    break;
  case MBC_MBC3:
    MBC_MBC3_Reset();
    break;
  case MBC_MBC5:
    MBC_MBC5_Reset();
    break;
  }
}

uint8 Cartridge::CPURead(uint16 address)
{
  switch (m_mbc)
  {
  case MBC_NONE:
    return MBC_NONE_Read(address);
  case MBC_MBC1:
    return MBC_MBC1_Read(address);
  case MBC_MBC3:
    return MBC_MBC3_Read(address);
  case MBC_MBC5:
    return MBC_MBC5_Read(address);
  }

  return 0x00;
}

void Cartridge::CPUWrite(uint16 address, uint8 value)
{
  switch (m_mbc)
  {
  case MBC_NONE:
    return MBC_NONE_Write(address, value);
  case MBC_MBC1:
    return MBC_MBC1_Write(address, value);
  case MBC_MBC3:
    return MBC_MBC3_Write(address, value);
  case MBC_MBC5:
    return MBC_MBC5_Write(address, value);
  }
}

bool Cartridge::LoadState(ByteStream* pStream, BinaryReader& binaryReader, Error* pError)
{
  uint32 crc = binaryReader.ReadUInt32();
  if (crc != m_crc)
  {
    pError->SetErrorUser(1, "CRC mismatch between save state cartridge and this cartridge");
    return false;
  }

  uint32 external_ram_size = binaryReader.ReadUInt32();
  if (m_external_ram_size != external_ram_size)
  {
    pError->SetErrorUser(1, "External ram size mismatch.");
    return false;
  }

  if (external_ram_size > 0)
    binaryReader.ReadBytes(m_external_ram, m_external_ram_size);

  bool has_timer = binaryReader.ReadBool();
  if (has_timer)
  {
    m_rtc_data.base_time = binaryReader.ReadUInt64();
    m_rtc_data.offset_days = binaryReader.ReadUInt16();
    m_rtc_data.offset_hours = binaryReader.ReadUInt8();
    m_rtc_data.offset_minutes = binaryReader.ReadUInt8();
    m_rtc_data.offset_seconds = binaryReader.ReadUInt8();
    m_rtc_data.active = binaryReader.ReadBool();
  }

  // MBC specific stuff follows
  uint32 ss_mbc = binaryReader.ReadUInt32();
  if (ss_mbc != (uint32)m_mbc)
  {
    pError->SetErrorUser(1, "MBC type mismatch");
    return false;
  }

  bool loadResult = false;
  switch (m_mbc)
  {
  case MBC_NONE:
    loadResult = MBC_NONE_LoadState(pStream, binaryReader);
    break;
  case MBC_MBC1:
    loadResult = MBC_MBC1_LoadState(pStream, binaryReader);
    break;
  case MBC_MBC3:
    loadResult = MBC_MBC3_LoadState(pStream, binaryReader);
    break;
  case MBC_MBC5:
    loadResult = MBC_MBC5_LoadState(pStream, binaryReader);
    break;
  }
  if (!loadResult)
  {
    pError->SetErrorUser(1, "MBC state load error");
    return false;
  }

  ss_mbc = binaryReader.ReadUInt32();
  if (ss_mbc != ~(uint32)m_mbc)
  {
    pError->SetErrorUser(1, "MBC trailing type mismatch");
    return false;
  }

  return true;
}

void Cartridge::SaveState(ByteStream* pStream, BinaryWriter& binaryWriter)
{
  binaryWriter.WriteUInt32(m_crc);
  binaryWriter.WriteUInt32(m_external_ram_size);

  // sram
  if (m_external_ram_size > 0)
    binaryWriter.WriteBytes(m_external_ram, m_external_ram_size);

  // timer
  binaryWriter.WriteBool(m_typeinfo->timer);
  if (m_typeinfo->timer)
  {
    binaryWriter.WriteUInt64(m_rtc_data.base_time);
    binaryWriter.WriteUInt16(m_rtc_data.offset_days);
    binaryWriter.WriteUInt8(m_rtc_data.offset_hours);
    binaryWriter.WriteUInt8(m_rtc_data.offset_minutes);
    binaryWriter.WriteUInt8(m_rtc_data.offset_seconds);
    binaryWriter.WriteBool(m_rtc_data.active);
  }

  // MBC specific stuff follows
  binaryWriter.WriteUInt32(m_mbc);
  switch (m_mbc)
  {
  case MBC_NONE:
    MBC_NONE_SaveState(pStream, binaryWriter);
    break;
  case MBC_MBC1:
    MBC_MBC1_SaveState(pStream, binaryWriter);
    break;
  case MBC_MBC3:
    MBC_MBC3_SaveState(pStream, binaryWriter);
    break;
  case MBC_MBC5:
    MBC_MBC5_SaveState(pStream, binaryWriter);
    break;
  }
  binaryWriter.WriteUInt32(~(uint32)m_mbc);
}

bool Cartridge::MBC_NONE_Init()
{
  if (m_num_rom_banks != 2)
  {
    Log_ErrorPrint("MBC_NONE expects 2 rom banks");
    return false;
  }

  // create external ram
  if (m_external_ram_size > 0)
  {
    m_external_ram = new byte[m_external_ram_size];
    Y_memzero(m_external_ram, m_external_ram_size);
  }

  MBC_NONE_Reset();
  return true;
}

void Cartridge::MBC_NONE_Reset() {}

uint8 Cartridge::MBC_NONE_Read(uint16 address)
{
  // Should have two banks
  switch (address & 0xF000)
  {
    // rom bank 0
  case 0x0000:
  case 0x1000:
  case 0x2000:
  case 0x3000:
    return m_rom_banks[0][address];

    // rom bank 1
  case 0x4000:
  case 0x5000:
  case 0x6000:
  case 0x7000:
    return m_rom_banks[1][address & 0x3FFF];

    // eram
  case 0xA000:
  case 0xB000:
  {
    if (m_external_ram != nullptr)
    {
      uint16 eram_offset = address - 0xA000;
      if (eram_offset < m_external_ram_size)
        return m_external_ram[eram_offset];
    }

    break;
  }
  }

  Log_WarningPrintf("MBC_NONE unhandled read from 0x%04X", address);
  return 0x00;
}

void Cartridge::MBC_NONE_Write(uint16 address, uint8 value)
{
  if (address >= 0xA000 && address < 0xC000)
  {
    if (m_external_ram != nullptr)
    {
      uint16 eram_offset = address - 0xA000;
      if (eram_offset < m_external_ram_size)
      {
        m_external_ram[eram_offset] = value;
        return;
      }
    }
  }

  // ignore all writes
  Log_WarningPrintf("MBC_NONE unhandled write to 0x%04X (value %02X)", address, value);
  return;
}

bool Cartridge::MBC_NONE_LoadState(ByteStream* pStream, BinaryReader& binaryReader)
{
  return true;
}

void Cartridge::MBC_NONE_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter)
{
  return;
}

bool Cartridge::MBC_MBC1_Init()
{
  // create external ram
  if (m_external_ram_size > 0)
  {
    m_external_ram = new byte[m_external_ram_size];
    Y_memzero(m_external_ram, m_external_ram_size);
  }

  MBC_MBC1_Reset();
  return true;
}

void Cartridge::MBC_MBC1_Reset()
{
  m_mbc_data.mbc1.ram_enable = false;
  m_mbc_data.mbc1.bank_mode = 0;
  m_mbc_data.mbc1.rom_bank_number = 1;
  m_mbc_data.mbc1.ram_bank_number = 0;
  MBC_MBC1_UpdateActiveBanks();
}

uint8 Cartridge::MBC_MBC1_Read(uint16 address)
{
  // Should have two banks
  switch (address & 0xF000)
  {
    // rom bank 0
  case 0x0000:
  case 0x1000:
  case 0x2000:
  case 0x3000:
    return m_rom_banks[0][address];

    // rom bank 1
  case 0x4000:
  case 0x5000:
  case 0x6000:
  case 0x7000:
    return m_rom_banks[m_mbc_data.mbc1.active_rom_bank][address & 0x3FFF];

    // eram
  case 0xA000:
  case 0xB000:
  {
    if (m_external_ram != nullptr && m_mbc_data.mbc1.ram_enable)
    {
      uint16 eram_offset = (uint16)m_mbc_data.mbc1.active_ram_bank * (uint16)8192 + (address - 0xA000);
      if (eram_offset < m_external_ram_size)
        return m_external_ram[eram_offset];
    }

    return 0x00;
  }
  }

  Log_WarningPrintf("MBC_MBC1 unhandled read from 0x%04X", address);
  return 0x00;
}

void Cartridge::MBC_MBC1_Write(uint16 address, uint8 value)
{
  switch (address & 0xF000)
  {
  case 0x0000:
  case 0x1000:
    m_mbc_data.mbc1.ram_enable = (value == 0x0A);
    TRACE("MBC1 ram %s", m_mbc_data.mbc1.ram_enable ? "enable" : "disable");
    if (!m_mbc_data.mbc1.ram_enable && m_external_ram_modified)
      SaveRAM();

    return;

  case 0x2000:
  case 0x3000:
    m_mbc_data.mbc1.rom_bank_number = value;
    MBC_MBC1_UpdateActiveBanks();
    return;

  case 0x4000:
  case 0x5000:
    m_mbc_data.mbc1.ram_bank_number = value;
    MBC_MBC1_UpdateActiveBanks();
    return;

  case 0x6000:
  case 0x7000:
    m_mbc_data.mbc1.bank_mode = value;
    MBC_MBC1_UpdateActiveBanks();
    return;
  }

  if (address >= 0xA000 && address < 0xC000)
  {
    if (m_external_ram != nullptr && m_mbc_data.mbc1.ram_enable)
    {
      uint16 eram_offset = (uint16)m_mbc_data.mbc1.active_ram_bank * (uint16)8192 + (address - 0xA000);
      if (eram_offset < m_external_ram_size)
      {
        if (m_external_ram[eram_offset] != value)
          m_external_ram_modified = true;

        m_external_ram[eram_offset] = value;
      }
    }

    return;
  }

  // ignore all writes
  Log_WarningPrintf("MBC_MBC1 unhandled write to 0x%04X (value %02X)", address, value);
  return;
}

bool Cartridge::MBC_MBC1_LoadState(ByteStream* pStream, BinaryReader& binaryReader)
{
  m_mbc_data.mbc1.active_rom_bank = binaryReader.ReadUInt8();
  m_mbc_data.mbc1.active_ram_bank = binaryReader.ReadUInt8();
  m_mbc_data.mbc1.ram_enable = binaryReader.ReadBool();
  m_mbc_data.mbc1.bank_mode = binaryReader.ReadUInt8();
  m_mbc_data.mbc1.rom_bank_number = binaryReader.ReadUInt8();
  m_mbc_data.mbc1.ram_bank_number = binaryReader.ReadUInt8();
  if (m_mbc_data.mbc1.active_rom_bank >= m_num_rom_banks)
    return false;

  return true;
}

void Cartridge::MBC_MBC1_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter)
{
  binaryWriter.WriteUInt8(m_mbc_data.mbc1.active_rom_bank);
  binaryWriter.WriteUInt8(m_mbc_data.mbc1.active_ram_bank);
  binaryWriter.WriteBool(m_mbc_data.mbc1.ram_enable);
  binaryWriter.WriteUInt8(m_mbc_data.mbc1.bank_mode);
  binaryWriter.WriteUInt8(m_mbc_data.mbc1.rom_bank_number);
  binaryWriter.WriteUInt8(m_mbc_data.mbc1.ram_bank_number);
}

void Cartridge::MBC_MBC1_UpdateActiveBanks()
{
  if (m_mbc_data.mbc1.bank_mode == 0)
  {
    m_mbc_data.mbc1.active_ram_bank = 0;
    m_mbc_data.mbc1.active_rom_bank = (m_mbc_data.mbc1.ram_bank_number << 5) | (m_mbc_data.mbc1.rom_bank_number & 0x1F);
  }
  else
  {
    m_mbc_data.mbc1.active_ram_bank = m_mbc_data.mbc1.ram_bank_number & 0x03;
    m_mbc_data.mbc1.active_rom_bank = m_mbc_data.mbc1.rom_bank_number;
  }

  // "But (when using the register below to specify the upper ROM Bank bits), the same happens for Bank 20h, 40h, and
  // 60h. Any attempt to address these ROM Banks will select Bank 21h, 41h, and 61h instead."
  if (m_mbc_data.mbc1.active_rom_bank == 0x00 || m_mbc_data.mbc1.active_rom_bank == 0x20 ||
      m_mbc_data.mbc1.active_rom_bank == 0x40 || m_mbc_data.mbc1.active_rom_bank == 0x60)
    m_mbc_data.mbc1.active_rom_bank++;

  // check ranges
  if (m_mbc_data.mbc1.active_rom_bank >= m_num_rom_banks)
  {
    Log_WarningPrintf("ROM bank out of range (%u / %u)", m_mbc_data.mbc1.active_rom_bank, m_num_rom_banks);
    m_mbc_data.mbc1.active_rom_bank = (uint8)m_num_rom_banks - 1;
  }

  TRACE("MBC1 ROM bank: %u", m_mbc_data.mbc1.active_rom_bank);
  TRACE("MBC1 RAM bank: %u", m_mbc_data.mbc1.active_ram_bank);
}

bool Cartridge::MBC_MBC3_Init()
{
  // create external ram
  if (m_external_ram_size > 0)
  {
    m_external_ram = new byte[m_external_ram_size];
    Y_memzero(m_external_ram, m_external_ram_size);
  }

  MBC_MBC3_Reset();
  return true;
}

void Cartridge::MBC_MBC3_Reset()
{
  m_mbc_data.mbc3.rom_bank_number = 1;
  m_mbc_data.mbc3.ram_bank_number = 0;
  m_mbc_data.mbc3.ram_rtc_enable = false;
  MBC_MBC3_UpdateActiveBanks();
}

uint8 Cartridge::MBC_MBC3_Read(uint16 address)
{
  // Should have two banks
  switch (address & 0xF000)
  {
    // rom bank 0
  case 0x0000:
  case 0x1000:
  case 0x2000:
  case 0x3000:
    return m_rom_banks[0][address];

    // rom bank 1
  case 0x4000:
  case 0x5000:
  case 0x6000:
  case 0x7000:
    return m_rom_banks[m_mbc_data.mbc3.rom_bank_number][address & 0x3FFF];

    // eram
  case 0xA000:
  case 0xB000:
  {
    if (m_mbc_data.mbc3.ram_rtc_enable)
    {
      if (m_mbc_data.mbc3.ram_bank_number <= 0x07)
      {
        uint16 eram_offset = (uint16)m_mbc_data.mbc3.ram_bank_number * (uint16)8192 + (address - 0xA000);
        if (eram_offset < m_external_ram_size)
          return m_external_ram[eram_offset];
      }
      else if (m_mbc_data.mbc3.ram_bank_number >= 0x08 && m_mbc_data.mbc3.ram_bank_number <= 0x0C)
      {
        uint8 rtc_register = m_mbc_data.mbc3.ram_bank_number - 0x08;
        return m_mbc_data.mbc3.rtc_latch_data[rtc_register];
      }
    }

    // ram not enabled
    return 0x00;
  }
  }

  Log_WarningPrintf("MBC_MBC3 unhandled read from 0x%04X", address);
  return 0x00;
}

void Cartridge::MBC_MBC3_Write(uint16 address, uint8 value)
{
  switch (address & 0xF000)
  {
  case 0x0000:
  case 0x1000:
    m_mbc_data.mbc3.ram_rtc_enable = (value == 0x0A);
    TRACE("MBC3 ram %s", m_mbc_data.mbc3.ram_rtc_enable ? "enable" : "disable");
    if (!m_mbc_data.mbc3.ram_rtc_enable && m_external_ram_modified)
      SaveRAM();

    return;

  case 0x2000:
  case 0x3000:
    m_mbc_data.mbc3.rom_bank_number = value & 0x7F;
    MBC_MBC3_UpdateActiveBanks();
    return;

  case 0x4000:
  case 0x5000:
    m_mbc_data.mbc3.ram_bank_number = value;
    MBC_MBC3_UpdateActiveBanks();
    return;

  case 0x6000:
  case 0x7000:
  {
    // When writing 00h, and then 01h to this register, the current time becomes latched into the RTC registers.
    if (m_mbc_data.mbc3.rtc_latch != 0x01 && value == 0x01)
    {
      // Latch the current time
      RTCValue current_time(GetCurrentRTCTime());
      m_mbc_data.mbc3.rtc_latch_data[0] = (uint8)current_time.seconds;       // 0x08
      m_mbc_data.mbc3.rtc_latch_data[1] = (uint8)current_time.minutes;       // 0x09
      m_mbc_data.mbc3.rtc_latch_data[2] = (uint8)current_time.hours;         // 0x0A
      m_mbc_data.mbc3.rtc_latch_data[3] = (uint8)(current_time.days & 0xFF); // 0x0B
      m_mbc_data.mbc3.rtc_latch_data[4] = (uint8)((current_time.days >> 8) & 0x01);
      m_mbc_data.mbc3.rtc_latch_data[4] |= ((uint8)(current_time.days >= 512) << 7); // 0x0C
    }

    // Update value
    m_mbc_data.mbc3.rtc_latch = value;
    return;
  }
  }

  if (address >= 0xA000 && address < 0xC000)
  {
    if (m_mbc_data.mbc3.ram_rtc_enable)
    {
      if (m_mbc_data.mbc3.ram_bank_number <= 0x07)
      {
        uint16 eram_offset = (uint16)m_mbc_data.mbc3.ram_bank_number * (uint16)8192 + (address - 0xA000);
        if (eram_offset < m_external_ram_size)
        {
          if (m_external_ram[eram_offset] != value)
            m_external_ram_modified = true;

          m_external_ram[eram_offset] = value;
        }
      }
      else if (m_mbc_data.mbc3.ram_bank_number >= 0x08 && m_mbc_data.mbc3.ram_bank_number <= 0x0C)
      {
        // RTC
        TRACE("RTC register write 0x%02X - 0x%02X (%u)", m_mbc_data.mbc3.ram_bank_number, value, value);
        switch (m_mbc_data.mbc3.ram_bank_number)
        {
        case 0x08:
        {
          if (m_rtc_data.offset_seconds != value)
          {
            m_rtc_data.offset_seconds = value;
            SaveRTC();
          }

          return;
        }

        case 0x09:
        {
          if (m_rtc_data.offset_minutes != value)
          {
            m_rtc_data.offset_minutes = value;
            SaveRTC();
          }

          return;
        }

        case 0x0A:
        {
          if (m_rtc_data.offset_hours != value)
          {
            m_rtc_data.offset_hours = value;
            SaveRTC();
          }

          return;
        }

        case 0x0B:
        {
          uint16 new_offset_days = (m_rtc_data.offset_days & 0x300) | (uint16)value;
          if (new_offset_days != m_rtc_data.offset_days)
          {
            m_rtc_data.offset_days = new_offset_days;
            SaveRTC();
          }

          return;
        }

        case 0x0C:
        {
          uint16 new_offset_days =
            (m_rtc_data.offset_days & 0xFF) | (uint16(value & 0x01) << 8) | (uint16(value & 0x80) << 2);
          if (new_offset_days != m_rtc_data.offset_days)
          {
            m_rtc_data.offset_days = new_offset_days;
            SaveRTC();
          }

          // disabling of timer not currently implemented.
          bool new_active = !(value & (1 << 6));
          m_rtc_data.active = new_active;
          return;
        }
        }
      }
    }

    // ram not enabled
    return;
  }

  // ignore all writes
  Log_WarningPrintf("MBC_MBC3 unhandled write to 0x%04X (value %02X)", address, value);
  return;
}

bool Cartridge::MBC_MBC3_LoadState(ByteStream* pStream, BinaryReader& binaryReader)
{
  m_mbc_data.mbc3.rom_bank_number = binaryReader.ReadUInt8();
  m_mbc_data.mbc3.ram_bank_number = binaryReader.ReadUInt8();
  m_mbc_data.mbc3.ram_rtc_enable = binaryReader.ReadBool();
  if (m_mbc_data.mbc3.rom_bank_number >= m_num_rom_banks)
    return false;

  return true;
}

void Cartridge::MBC_MBC3_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter)
{
  binaryWriter.WriteUInt8(m_mbc_data.mbc3.rom_bank_number);
  binaryWriter.WriteUInt8(m_mbc_data.mbc3.ram_bank_number);
  binaryWriter.WriteBool(m_mbc_data.mbc3.ram_rtc_enable);
}

void Cartridge::MBC_MBC3_UpdateActiveBanks()
{
  // Same as for MBC1, except that the whole 7 bits of the RAM Bank Number are written directly to this address. As for
  // the MBC1, writing a value of 00h, will select Bank 01h instead. All other values 01-7Fh select the corresponding
  // ROM Banks.
  if (m_mbc_data.mbc3.rom_bank_number == 0x00)
    m_mbc_data.mbc3.rom_bank_number++;

  // check ranges
  if (m_mbc_data.mbc3.rom_bank_number >= m_num_rom_banks)
  {
    Log_WarningPrintf("ROM bank out of range (%u / %u)", m_mbc_data.mbc3.rom_bank_number, m_num_rom_banks);
    m_mbc_data.mbc3.rom_bank_number = (uint8)m_num_rom_banks - 1;
  }

  TRACE("MBC3 ROM bank: %u", m_mbc_data.mbc3.rom_bank_number);
  TRACE("MBC3 RAM bank: %u", m_mbc_data.mbc3.ram_bank_number);
}

bool Cartridge::MBC_MBC5_Init()
{
  // create external ram
  if (m_external_ram_size > 0)
  {
    m_external_ram = new byte[m_external_ram_size];
    Y_memzero(m_external_ram, m_external_ram_size);
  }

  MBC_MBC5_Reset();
  return true;
}

void Cartridge::MBC_MBC5_Reset()
{
  m_mbc_data.mbc5.rom_bank_number = 1;
  m_mbc_data.mbc5.ram_bank_number = 0;
  m_mbc_data.mbc5.ram_enable = false;
  MBC_MBC5_UpdateActiveBanks();
}

uint8 Cartridge::MBC_MBC5_Read(uint16 address)
{
  // Should have two banks
  switch (address & 0xF000)
  {
    // rom bank 0
  case 0x0000:
  case 0x1000:
  case 0x2000:
  case 0x3000:
    return m_rom_banks[0][address];

    // rom bank 1
  case 0x4000:
  case 0x5000:
  case 0x6000:
  case 0x7000:
    return m_rom_banks[m_mbc_data.mbc5.active_rom_bank][address & 0x3FFF];

    // eram
  case 0xA000:
  case 0xB000:
  {
    if (m_mbc_data.mbc5.ram_enable)
    {
      uint16 eram_offset = (uint16)m_mbc_data.mbc5.ram_bank_number * (uint16)8192 + (address - 0xA000);
      if (eram_offset < m_external_ram_size)
        return m_external_ram[eram_offset];
    }

    // ram not enabled
    return 0x00;
  }
  }

  Log_WarningPrintf("MBC_MBC5 unhandled read from 0x%04X", address);
  return 0x00;
}

void Cartridge::MBC_MBC5_Write(uint16 address, uint8 value)
{
  switch (address & 0xF000)
  {
  case 0x0000:
  case 0x1000:
    m_mbc_data.mbc5.ram_enable = (value == 0x0A);
    TRACE("MBC5 ram %s", m_mbc_data.mbc5.ram_enable ? "enable" : "disable");
    if (!m_mbc_data.mbc5.ram_enable && m_external_ram_modified)
      SaveRAM();

    return;

  case 0x2000:
    m_mbc_data.mbc5.rom_bank_number = (m_mbc_data.mbc5.rom_bank_number & 0x100) | (uint16)value;
    MBC_MBC5_UpdateActiveBanks();
    return;

  case 0x3000:
    m_mbc_data.mbc5.rom_bank_number = (m_mbc_data.mbc5.rom_bank_number & 0xFF) | ((uint16)(value & 0x01) << 8);
    MBC_MBC5_UpdateActiveBanks();
    return;

  case 0x4000:
  case 0x5000:
    m_mbc_data.mbc5.ram_bank_number = value;
    MBC_MBC5_UpdateActiveBanks();
    return;
  }

  if (address >= 0xA000 && address < 0xC000)
  {
    if (m_mbc_data.mbc5.ram_enable)
    {
      uint16 eram_offset = (uint16)m_mbc_data.mbc5.ram_bank_number * (uint16)8192 + (address - 0xA000);
      if (eram_offset < m_external_ram_size)
      {
        if (m_external_ram[eram_offset] != value)
          m_external_ram_modified = true;

        m_external_ram[eram_offset] = value;
      }
    }

    // ram not enabled
    return;
  }

  // ignore all writes
  Log_WarningPrintf("MBC_MBC5 unhandled write to 0x%04X (value %02X)", address, value);
  return;
}

bool Cartridge::MBC_MBC5_LoadState(ByteStream* pStream, BinaryReader& binaryReader)
{
  m_mbc_data.mbc5.active_rom_bank = binaryReader.ReadUInt16();
  m_mbc_data.mbc5.rom_bank_number = binaryReader.ReadUInt16();
  m_mbc_data.mbc5.ram_bank_number = binaryReader.ReadUInt8();
  m_mbc_data.mbc5.ram_enable = binaryReader.ReadBool();
  if (m_mbc_data.mbc5.active_rom_bank >= m_num_rom_banks)
    return false;

  return true;
}

void Cartridge::MBC_MBC5_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter)
{
  binaryWriter.WriteUInt16(m_mbc_data.mbc5.active_rom_bank);
  binaryWriter.WriteUInt16(m_mbc_data.mbc5.rom_bank_number);
  binaryWriter.WriteUInt8(m_mbc_data.mbc5.ram_bank_number);
  binaryWriter.WriteBool(m_mbc_data.mbc5.ram_enable);
}

void Cartridge::MBC_MBC5_UpdateActiveBanks()
{
  // Same as for MBC1, except that accessing up to bank 1E0h is supported now. Also, bank 0 is actually bank 0.
  m_mbc_data.mbc5.active_rom_bank = m_mbc_data.mbc5.rom_bank_number;
  if (m_mbc_data.mbc5.active_rom_bank >= m_num_rom_banks)
  {
    // since this is written as two different values, provided PC isn't in cart space,
    // it may be temporarily out of range, and this is okay, it'll be fixed afterwards
    m_mbc_data.mbc5.active_rom_bank = (uint8)m_num_rom_banks - 1;
  }

  TRACE("MBC5 ROM bank: %u", m_mbc_data.mbc5.rom_bank_number);
  TRACE("MBC5 RAM bank: %u", m_mbc_data.mbc5.ram_bank_number);
}
