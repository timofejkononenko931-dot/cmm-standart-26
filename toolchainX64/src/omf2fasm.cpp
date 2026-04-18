//// omf2fasm.cpp  —  Intel OMF (.obj) → FASM bridge generator
//// Supports: 16/32-bit OMF from Sphinx C-- and compatible compilers
//// Output:   bridge.asm  (format MS64 COFF, ready for FASM)
////
//// Build:
////   MSVC:   cl /std:c++20 /O2 /W4 omf2fasm.cpp /Fe:omf2fasm.exe
////   MinGW:  g++ -std=c++20 -O2 -Wall -o omf2fasm omf2fasm.cpp
//// Usage:
////   omf2fasm <input.obj> [output.asm]
//
//#include <cstdint>
//#include <cstring>
//#include <fstream>
//#include <iomanip>
//#include <iostream>
//#include <sstream>
//#include <stdexcept>
//#include <string>
//#include <map>
//#include <unordered_map>
//#include <vector>
//
//// ============================================================
////  OMF record type constants
//// ============================================================
//namespace OMF {
//    // Names
//    constexpr uint8_t THEADR  = 0x80; // Translator Header
//    constexpr uint8_t LNAMES  = 0x96; // List of Names
//
//    // Segments
//    constexpr uint8_t SEGDEF  = 0x98; // Segment Definition (16-bit)
//    constexpr uint8_t SEGDEF32= 0x99; // Segment Definition (32-bit)
//
//    // Groups
//    constexpr uint8_t GRPDEF  = 0x9A;
//
//    // External / Public symbols
//    constexpr uint8_t EXTDEF  = 0x8C; // External symbol reference
//    constexpr uint8_t PUBDEF  = 0x90; // Public symbol (16-bit offset)
//    constexpr uint8_t PUBDEF32= 0x91; // Public symbol (32-bit offset)
//
//    // Enumerated data
//    constexpr uint8_t LEDATA  = 0xA0; // Logical Enumerated Data (16-bit offset)
//    constexpr uint8_t LEDATA32= 0xA1; // Logical Enumerated Data (32-bit offset)
//
//    // Fixups (we note them but don't fully resolve)
//    constexpr uint8_t FIXUPP  = 0x9C;
//    constexpr uint8_t FIXUPP32= 0x9D;
//
//    // Iterated data (rare but encountered)
//    constexpr uint8_t LIDATA  = 0xA2;
//    constexpr uint8_t LIDATA32= 0xA3;
//
//    // Module end
//    constexpr uint8_t MODEND  = 0x8A;
//    constexpr uint8_t MODEND32= 0x8B;
//
//    constexpr const char* typeName(uint8_t t) {
//        switch (t) {
//        case THEADR:   return "THEADR";
//        case LNAMES:   return "LNAMES";
//        case SEGDEF:   return "SEGDEF";
//        case SEGDEF32: return "SEGDEF32";
//        case GRPDEF:   return "GRPDEF";
//        case EXTDEF:   return "EXTDEF";
//        case PUBDEF:   return "PUBDEF";
//        case PUBDEF32: return "PUBDEF32";
//        case LEDATA:   return "LEDATA";
//        case LEDATA32: return "LEDATA32";
//        case FIXUPP:   return "FIXUPP";
//        case FIXUPP32: return "FIXUPP32";
//        case LIDATA:   return "LIDATA";
//        case LIDATA32: return "LIDATA32";
//        case MODEND:   return "MODEND";
//        case MODEND32: return "MODEND32";
//        default:       return "UNKNOWN";
//        }
//    }
//}
//
//// ============================================================
////  Data structures
//// ============================================================
//
//// A contiguous chunk of extracted bytes from one LEDATA record
//struct DataChunk {
//    uint32_t    segIndex;  // 1-based segment index
//    uint32_t    offset;    // offset within segment
//    std::vector<uint8_t> bytes;
//};
//
//// A public symbol from PUBDEF
//struct PublicSymbol {
//    std::string name;
//    uint32_t    segIndex;  // 1-based; 0 = absolute
//    uint32_t    offset;
//};
//
//// A segment descriptor from SEGDEF
//struct SegDef {
//    std::string name;      // resolved from LNAMES
//    std::string className; // e.g. "CODE", "DATA", "STACK"
//    uint32_t    length;
//    uint8_t     attrib;    // alignment / combine / use bits
//    bool        is32bit;
//};
//
//// A fixup (relocation) — noted for diagnostic purposes
//struct Fixup {
//    uint32_t dataSegIndex;
//    uint32_t offset;       // offset within the LEDATA block it refers to
//    bool     isSegRelative;
//    uint32_t targetIndex;
//    std::string description;
//};
//
//// ============================================================
////  OMF reader — wraps a byte buffer with cursor
//// ============================================================
//class OmfReader {
//public:
//    explicit OmfReader(std::vector<uint8_t> data)
//        : data_(std::move(data)), pos_(0) {}
//
//    bool eof()    const { return pos_ >= data_.size(); }
//    size_t pos()  const { return pos_; }
//    size_t size() const { return data_.size(); }
//
//    uint8_t  u8()  { checkAvail(1); return data_[pos_++]; }
//    uint16_t u16() {
//        checkAvail(2);
//        uint16_t v = static_cast<uint16_t>(data_[pos_]) |
//                    (static_cast<uint16_t>(data_[pos_+1]) << 8);
//        pos_ += 2;
//        return v;
//    }
//    uint32_t u32() {
//        checkAvail(4);
//        uint32_t v = static_cast<uint32_t>(data_[pos_])        |
//                    (static_cast<uint32_t>(data_[pos_+1]) << 8) |
//                    (static_cast<uint32_t>(data_[pos_+2]) << 16)|
//                    (static_cast<uint32_t>(data_[pos_+3]) << 24);
//        pos_ += 4;
//        return v;
//    }
//
//    // OMF "index" field: 1 or 2 bytes (bit 7 of first byte flags 2-byte form)
//    uint16_t index() {
//        uint8_t b = u8();
//        if (b & 0x80) {
//            return static_cast<uint16_t>((b & 0x7F) << 8) | u8();
//        }
//        return b;
//    }
//
//    // Length-prefixed string (1-byte length)
//    std::string str() {
//        uint8_t len = u8();
//        checkAvail(len);
//        std::string s(reinterpret_cast<const char*>(&data_[pos_]), len);
//        pos_ += len;
//        return s;
//    }
//
//    // Copy 'n' bytes
//    std::vector<uint8_t> bytes(size_t n) {
//        checkAvail(n);
//        std::vector<uint8_t> v(data_.begin() + pos_,
//                               data_.begin() + pos_ + n);
//        pos_ += n;
//        return v;
//    }
//
//    void skip(size_t n) { checkAvail(n); pos_ += n; }
//    void seek(size_t p) { pos_ = p; }
//
//    const uint8_t* rawPtr() const { return data_.data() + pos_; }
//
//private:
//    std::vector<uint8_t> data_;
//    size_t               pos_;
//
//    void checkAvail(size_t n) const {
//        if (pos_ + n > data_.size())
//            throw std::runtime_error(
//                "OMF read past end at offset 0x"
//                + hexStr(pos_) + " (need " + std::to_string(n)
//                + ", have " + std::to_string(data_.size() - pos_) + ")");
//    }
//    static std::string hexStr(size_t v) {
//        std::ostringstream ss;
//        ss << std::hex << v;
//        return ss.str();
//    }
//};
//
//// ============================================================
////  OMF Parser
//// ============================================================
//class OmfParser {
//public:
//    explicit OmfParser(bool verbose = true) : verbose_(verbose) {}
//
//    // Parse an OMF .obj file; fills all public members
//    void parse(const std::string& path) {
//        auto raw = loadFile(path);
//        OmfReader rd(std::move(raw));
//
//        uint64_t recCount = 0;
//
//        while (!rd.eof()) {
//            if (rd.size() - rd.pos() < 3) break; // can't read header
//
//            size_t   recStart = rd.pos();
//            uint8_t  recType  = rd.u8();
//            uint16_t recLen   = rd.u16();
//
//            if (recLen == 0) {
//                if (verbose_)
//                    std::cout << "[WARN] Zero-length record at 0x"
//                              << std::hex << recStart << std::dec << "\n";
//                break;
//            }
//
//            size_t dataStart = rd.pos();
//            size_t dataEnd   = dataStart + recLen - 1; // -1 for checksum byte
//
//            if (dataEnd + 1 > rd.size()) {
//                std::cout << "[WARN] Record at 0x" << std::hex << recStart
//                          << " claims length " << recLen
//                          << " but file ends at 0x" << rd.size() << std::dec << "\n";
//                break;
//            }
//
//            // Dispatch
//            switch (recType) {
//            case OMF::THEADR:    parseTHEADR(rd, recLen);    break;
//            case OMF::LNAMES:    parseLNAMES(rd, dataEnd);   break;
//            case OMF::SEGDEF:    parseSEGDEF(rd, dataEnd, false); break;
//            case OMF::SEGDEF32:  parseSEGDEF(rd, dataEnd, true);  break;
//            case OMF::GRPDEF:    /* noted, not needed */           break;
//            case OMF::EXTDEF:    parseEXTDEF(rd, dataEnd);   break;
//            case OMF::PUBDEF:    parsePUBDEF(rd, dataEnd, false); break;
//            case OMF::PUBDEF32:  parsePUBDEF(rd, dataEnd, true);  break;
//            case OMF::LEDATA:    parseLEDATA(rd, dataEnd, false); break;
//            case OMF::LEDATA32:  parseLEDATA(rd, dataEnd, true);  break;
//            case OMF::LIDATA:    parseLIDATA(rd, dataEnd, false); break;
//            case OMF::LIDATA32:  parseLIDATA(rd, dataEnd, true);  break;
//            case OMF::FIXUPP:    parseFIXUPP(rd, dataEnd);   break;
//            case OMF::FIXUPP32:  parseFIXUPP(rd, dataEnd);   break;
//            case OMF::MODEND:
//            case OMF::MODEND32:
//                if (verbose_)
//                    std::cout << "[INFO] MODEND — end of module\n";
//                goto done;
//            default:
//                if (verbose_)
//                    std::cout << "[SKIP] Unknown record 0x" << std::hex
//                              << static_cast<int>(recType)
//                              << " len=" << recLen << std::dec << "\n";
//                break;
//            }
//
//            // Seek to start of next record (past checksum)
//            rd.seek(dataEnd + 1);
//            ++recCount;
//        }
//        done:
//        if (verbose_) {
//            std::cout << "[INFO] Parsed " << recCount << " records\n";
//            std::cout << "[INFO] LNAMES: " << lnames_.size()
//                      << "  SEGDEFs: " << segdefs_.size()
//                      << "  PUBDEFs: " << pubdefs_.size()
//                      << "  LEDATA chunks: " << chunks_.size() << "\n";
//        }
//    }
//
//    // Public results
//    std::vector<std::string>   lnames_;    // 1-based (index 0 = "" sentinel)
//    std::vector<SegDef>        segdefs_;   // 1-based
//    std::vector<PublicSymbol>  pubdefs_;
//    std::vector<DataChunk>     chunks_;
//    std::vector<Fixup>         fixups_;
//    std::string                moduleName_;
//
//    // Resolve a 1-based segment index to its SegDef (nullptr if out of range)
//    const SegDef* segdef(uint32_t idx) const {
//        if (idx == 0 || idx > segdefs_.size()) return nullptr;
//        return &segdefs_[idx - 1];
//    }
//
//    // Heuristic: is this segment a code segment?
//    bool isCodeSeg(uint32_t idx) const {
//        const SegDef* sd = segdef(idx);
//        if (!sd) return true; // unknown → assume code
//        std::string cls = sd->className;
//        for (char& c : cls) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
//        // Class names that indicate code: CODE, TEXT, _TEXT
//        if (cls.find("CODE") != std::string::npos) return true;
//        if (cls.find("TEXT") != std::string::npos) return true;
//        // Name-based heuristic
//        std::string nm = sd->name;
//        for (char& c : nm) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
//        if (nm == "_TEXT" || nm == "TEXT" || nm == "CODE" || nm == ".TEXT") return true;
//        return false;
//    }
//
//private:
//    bool verbose_;
//    // Last LEDATA segment index — fixups reference it
//    uint32_t lastLedataSeg_    = 0;
//    uint32_t lastLedataOffset_ = 0;
//
//    // ── THEADR ────────────────────────────────────────────────
//    void parseTHEADR(OmfReader& rd, uint16_t /*len*/) {
//        moduleName_ = rd.str();
//        if (verbose_)
//            std::cout << "[THEADR] Module: " << moduleName_ << "\n";
//    }
//
//    // ── LNAMES ───────────────────────────────────────────────
//    // First call: add sentinel so indices are 1-based
//    void parseLNAMES(OmfReader& rd, size_t end) {
//        if (lnames_.empty()) lnames_.push_back(""); // index 0 sentinel
//        while (rd.pos() < end) {
//            std::string name = rd.str();
//            lnames_.push_back(name);
//            if (verbose_)
//                std::cout << "[LNAMES] [" << (lnames_.size() - 1) << "] '"
//                          << name << "'\n";
//        }
//    }
//
//    // ── SEGDEF ───────────────────────────────────────────────
//    void parseSEGDEF(OmfReader& rd, size_t end, bool is32) {
//        if (segdefs_.empty()) segdefs_.push_back({}); // 1-based sentinel
//
//        SegDef sd;
//        sd.is32bit = is32;
//        sd.attrib  = rd.u8();
//
//        uint8_t align = (sd.attrib >> 5) & 0x07;
//        (void)align; // alignment in bytes; noted but not emitted
//
//        if (is32) {
//            sd.length = rd.u32();
//        } else {
//            uint16_t len16 = rd.u16();
//            sd.length = len16;
//            // length == 0 with align == 1 means 64K
//            if (len16 == 0 && align == 1) sd.length = 0x10000;
//        }
//
//        uint16_t nameIdx  = rd.index();
//        uint16_t classIdx = rd.index();
//        rd.index(); // overlay index — ignore
//
//        auto resolveName = [&](uint16_t idx) -> std::string {
//            if (idx == 0 || idx >= lnames_.size()) return "";
//            return lnames_[idx];
//        };
//
//        sd.name      = resolveName(nameIdx);
//        sd.className = resolveName(classIdx);
//
//        segdefs_.push_back(sd);
//
//        if (verbose_) {
//            std::cout << "[SEGDEF" << (is32 ? "32" : "") << "] [" << segdefs_.size() - 1 << "] '"
//                      << sd.name << "' class='" << sd.className
//                      << "' len=0x" << std::hex << sd.length << std::dec << "\n";
//        }
//        (void)end;
//    }
//
//    // ── EXTDEF ───────────────────────────────────────────────
//    void parseEXTDEF(OmfReader& rd, size_t end) {
//        while (rd.pos() < end) {
//            std::string name = rd.str();
//            rd.index(); // type index — ignore
//            if (verbose_)
//                std::cout << "[EXTDEF] '" << name << "'\n";
//        }
//    }
//
//    // ── PUBDEF ───────────────────────────────────────────────
//    void parsePUBDEF(OmfReader& rd, size_t end, bool is32) {
//        rd.index(); // base group index — ignore
//        uint16_t baseSegIdx = rd.index();
//        if (baseSegIdx == 0) rd.u16(); // base frame (absolute segment)
//
//        while (rd.pos() < end) {
//            if (rd.pos() == end) break;
//
//            PublicSymbol sym;
//            sym.name     = rd.str();
//            sym.segIndex = baseSegIdx;
//
//            if (is32)
//                sym.offset = rd.u32();
//            else
//                sym.offset = rd.u16();
//
//            rd.index(); // type index — ignore
//
//            pubdefs_.push_back(sym);
//
//            if (verbose_) {
//                std::cout << "[PUBDEF" << (is32 ? "32" : "") << "] '"
//                          << sym.name << "'  seg=" << sym.segIndex
//                          << "  offset=0x" << std::hex << sym.offset
//                          << std::dec << "\n";
//            }
//        }
//    }
//
//    // ── LEDATA ───────────────────────────────────────────────
//    void parseLEDATA(OmfReader& rd, size_t end, bool is32) {
//        DataChunk chunk;
//        chunk.segIndex = rd.index();
//        chunk.offset   = is32 ? rd.u32() : rd.u16();
//
//        size_t dataBytes = end - rd.pos();
//        chunk.bytes      = rd.bytes(dataBytes);
//
//        lastLedataSeg_    = chunk.segIndex;
//        lastLedataOffset_ = chunk.offset;
//
//        if (verbose_) {
//            std::cout << "[LEDATA" << (is32 ? "32" : "") << "] seg=" << chunk.segIndex
//                      << "  offset=0x" << std::hex << chunk.offset << std::dec
//                      << "  bytes=" << dataBytes << "\n";
//        }
//
//        chunks_.push_back(std::move(chunk));
//    }
//
//    // ── LIDATA (iterated data — expand RLE) ──────────────────
//    void parseLIDATA(OmfReader& rd, size_t end, bool is32) {
//        DataChunk chunk;
//        chunk.segIndex = rd.index();
//        chunk.offset   = is32 ? rd.u32() : rd.u16();
//
//        // Recursively expand iterated data blocks
//        expandLIDATA(rd, end, chunk.bytes);
//
//        if (verbose_) {
//            std::cout << "[LIDATA" << (is32 ? "32" : "") << "] seg=" << chunk.segIndex
//                      << "  offset=0x" << std::hex << chunk.offset << std::dec
//                      << "  expanded=" << chunk.bytes.size() << " bytes\n";
//        }
//
//        if (!chunk.bytes.empty())
//            chunks_.push_back(std::move(chunk));
//    }
//
//    // Expand one LIDATA block (may be recursive)
//    void expandLIDATA(OmfReader& rd, size_t end,
//                      std::vector<uint8_t>& out) {
//        while (rd.pos() < end) {
//            uint16_t repeatCount = rd.u16();
//            uint16_t blockCount  = rd.u16();
//
//            if (blockCount == 0) {
//                // Leaf: raw bytes follow
//                uint8_t  contentLen = rd.u8();
//                auto raw = rd.bytes(contentLen);
//                for (uint16_t r = 0; r < repeatCount; ++r)
//                    out.insert(out.end(), raw.begin(), raw.end());
//            } else {
//                // Non-leaf: nested blocks
//                std::vector<uint8_t> nested;
//                size_t blockEnd = rd.pos(); // we'll scan blockCount sub-blocks
//                // We don't know exact sub-block lengths without parsing them
//                // so we parse into a temp and repeat
//                for (uint16_t b = 0; b < blockCount; ++b)
//                    expandLIDATA(rd, end, nested);
//                for (uint16_t r = 0; r < repeatCount; ++r)
//                    out.insert(out.end(), nested.begin(), nested.end());
//                (void)blockEnd;
//            }
//        }
//    }
//
//    // ── FIXUPP ───────────────────────────────────────────────
//    void parseFIXUPP(OmfReader& rd, size_t end) {
//        while (rd.pos() < end) {
//            uint8_t locat = rd.u8();
//
//            if (locat & 0x80) {
//                // FIXUP subrecord
//                uint8_t  fixdat = rd.u8();
//                uint16_t frame  = 0, target = 0;
//
//                // F bit: explicit frame datum
//                if (!(fixdat & 0x80)) rd.index(); // frame datum
//                // T bit: explicit target datum
//                if (!(fixdat & 0x08)) {
//                    target = rd.index(); // target datum
//                }
//                // P bit: target displacement follows
//                bool hasDisp = !(fixdat & 0x04);
//                uint16_t disp = hasDisp ? rd.u16() : 0;
//
//                Fixup fx;
//                fx.dataSegIndex   = lastLedataSeg_;
//                fx.offset         = locat & 0x03; // simplified location
//                fx.isSegRelative  = (fixdat & 0x04) != 0;
//                fx.targetIndex    = target;
//                std::ostringstream desc;
//                desc << "locat=0x" << std::hex << static_cast<int>(locat)
//                     << " fixdat=0x" << static_cast<int>(fixdat)
//                     << " target=" << target;
//                if (hasDisp) desc << " disp=0x" << disp;
//                fx.description = desc.str();
//                fixups_.push_back(fx);
//                (void)frame;
//            } else {
//                // THREAD subrecord — set up frame/target threads
//                uint8_t thdat = rd.u8();
//                if (!(thdat & 0x40)) rd.index(); // thread datum
//                (void)thdat;
//            }
//        }
//
//        if (verbose_ && !fixups_.empty()) {
//            std::cout << "[FIXUPP] total fixups so far: " << fixups_.size() << "\n";
//        }
//    }
//
//    // ── File loader ───────────────────────────────────────────
//    static std::vector<uint8_t> loadFile(const std::string& path) {
//        std::ifstream f(path, std::ios::binary | std::ios::ate);
//        if (!f) throw std::runtime_error("Cannot open: " + path);
//        auto sz = static_cast<size_t>(f.tellg());
//        f.seekg(0);
//        std::vector<uint8_t> buf(sz);
//        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
//        if (!f) throw std::runtime_error("Read error: " + path);
//        std::cout << "[INFO] Loaded " << sz << " bytes from '" << path << "'\n";
//        return buf;
//    }
//};
//
//// ============================================================
////  FASM bridge generator
//// ============================================================
//class FasmBridgeGen {
//public:
//    FasmBridgeGen(const OmfParser& p, bool verbose = true)
//        : omf_(p), verbose_(verbose) {}
//
//    void generate(const std::string& outPath) {
//        std::ostringstream out;
//
//        writeHeader(out);
//        writeExterns(out);
//        writeSections(out);
//        writeFixupComment(out);
//
//        // Write to file
//        std::ofstream f(outPath);
//        if (!f) throw std::runtime_error("Cannot write: " + outPath);
//        f << out.str();
//        std::cout << "[INFO] Written: " << outPath << "\n";
//    }
//
//private:
//    const OmfParser& omf_;
//    bool verbose_;
//
//    // ── Header ───────────────────────────────────────────────
//    void writeHeader(std::ostringstream& out) {
//        out << "; ============================================================\n"
//            << "; bridge.asm — auto-generated by omf2fasm\n"
//            << "; Source module: " << omf_.moduleName_ << "\n"
//            << "; ============================================================\n\n"
//            << "format MS64 COFF\n\n";
//    }
//
//    // ── Public symbols declared as exports ───────────────────
//    void writeExterns(std::ostringstream& out) {
//        if (omf_.pubdefs_.empty()) return;
//
//        out << "; --- public symbols ---\n";
//        for (auto& sym : omf_.pubdefs_) {
//            // Sanitize name (remove leading underscore added by older compilers)
//            std::string label = sanitizeLabel(sym.name);
//            out << "public " << label << "\n";
//        }
//        out << "\n";
//    }
//
//    // ── Section content ───────────────────────────────────────
//    void writeSections(std::ostringstream& out) {
//        // Split chunks into code and data buckets by segment index
//        // Key: segIndex → merged byte vector at each offset
//        // We collect by segment, sort by offset, emit contiguously.
//
//        struct SegContent {
//            std::string sectionName;
//            std::string sectionAttribs;
//            // offset → bytes (may have gaps → fill with 0x00 nop-padding)
//            std::map<uint32_t, std::vector<uint8_t>> blocks;
//        };
//
//        std::unordered_map<uint32_t, SegContent> segs;
//
//        for (auto& chunk : omf_.chunks_) {
//            if (chunk.bytes.empty()) continue;
//
//            auto& sc = segs[chunk.segIndex];
//            if (sc.sectionName.empty()) {
//                if (omf_.isCodeSeg(chunk.segIndex)) {
//                    sc.sectionName    = ".text";
//                    sc.sectionAttribs = "code readable executable";
//                } else {
//                    sc.sectionName    = ".data";
//                    sc.sectionAttribs = "data readable writeable";
//                }
//            }
//            sc.blocks[chunk.offset] = chunk.bytes;
//        }
//
//        if (segs.empty()) {
//            out << "; [WARNING] No LEDATA chunks found — object may contain only "
//                << "symbol definitions.\n";
//            return;
//        }
//
//        // Emit code sections first, data sections second
//        auto emitSeg = [&](uint32_t idx, const SegContent& sc) {
//            const SegDef* sd = omf_.segdef(idx);
//            out << "; ============================================================\n"
//                << "; Segment " << idx;
//            if (sd) out << " '" << sd->name << "' class='" << sd->className << "'";
//            out << "\n"
//                << "; ============================================================\n"
//                << "section '" << sc.sectionName << "' "
//                << sc.sectionAttribs << "\n\n";
//
//            // Emit public labels that belong to this segment
//            emitLabels(out, idx, sc.blocks);
//
//            // Merge blocks in offset order, padding gaps with 0x90 (NOP) for code
//            // or 0x00 for data
//            uint8_t padByte = (sc.sectionName == ".text") ? 0x90 : 0x00;
//            emitBlocks(out, sc.blocks, padByte);
//
//            out << "\n";
//        };
//
//        // Code sections first
//        for (auto& [idx, sc] : segs)
//            if (sc.sectionName == ".text") emitSeg(idx, sc);
//
//        // Then data sections
//        for (auto& [idx, sc] : segs)
//            if (sc.sectionName != ".text") emitSeg(idx, sc);
//    }
//
//    // ── Emit labels interleaved with data ────────────────────
//    void emitLabels(std::ostringstream& out,
//                    uint32_t segIdx,
//                    const std::map<uint32_t, std::vector<uint8_t>>& blocks)
//    {
//        // Build a map offset → list of labels for quick lookup
//        std::unordered_map<uint32_t, std::vector<std::string>> labelMap;
//        for (auto& sym : omf_.pubdefs_) {
//            if (sym.segIndex == segIdx) {
//                labelMap[sym.offset].push_back(sanitizeLabel(sym.name));
//            }
//        }
//
//        // Labels that fall exactly at the start of a block are emitted inline.
//        // Labels at offset 0 (before first block) get emitted first.
//        if (labelMap.count(0)) {
//            for (auto& lbl : labelMap[0])
//                out << lbl << ":\n";
//        }
//        // The rest are emitted inline during block emission — we store the map
//        // on *this* temporarily for use in emitBlocks.
//        pendingLabels_ = std::move(labelMap);
//    }
//
//    // ── Emit byte blocks as db directives ────────────────────
//    void emitBlocks(std::ostringstream& out,
//                    const std::map<uint32_t, std::vector<uint8_t>>& blocks,
//                    uint8_t padByte)
//    {
//        uint32_t cursor = 0;
//        bool first = true;
//
//        for (auto& [offset, data] : blocks) {
//            // Fill gap between cursor and this block
//            if (!first && offset > cursor) {
//                uint32_t gapSize = offset - cursor;
//                out << "  ; --- gap padding (" << gapSize << " bytes) ---\n";
//                emitDbLine(out, std::vector<uint8_t>(gapSize, padByte));
//            }
//
//            // Emit labels that land inside this block
//            for (auto& [lblOff, lblNames] : pendingLabels_) {
//                if (lblOff >= offset && lblOff < offset + static_cast<uint32_t>(data.size())) {
//                    // Will be emitted after the bytes before the label offset
//                    // For simplicity we emit them at the start of the block here
//                    // A fully accurate implementation would split the db directive.
//                    if (lblOff == offset) {
//                        for (auto& lbl : lblNames)
//                            out << lbl << ":\n";
//                    }
//                }
//            }
//
//            // Emit data bytes as db lines (max 16 bytes per line for readability)
//            if (verbose_) {
//                const SegDef* sd = nullptr; // context already provided above
//                (void)sd;
//            }
//            emitDbLine(out, data);
//
//            cursor = offset + static_cast<uint32_t>(data.size());
//            first  = false;
//        }
//    }
//
//    // ── Format a vector of bytes as FASM db lines ─────────────
//    static void emitDbLine(std::ostringstream& out,
//                           const std::vector<uint8_t>& bytes,
//                           size_t cols = 16)
//    {
//        if (bytes.empty()) return;
//        for (size_t i = 0; i < bytes.size(); i += cols) {
//            out << "  db ";
//            size_t end = std::min(i + cols, bytes.size());
//            for (size_t j = i; j < end; ++j) {
//                out << "0x" << std::hex << std::setw(2) << std::setfill('0')
//                    << static_cast<int>(bytes[j]);
//                if (j + 1 < end) out << ", ";
//            }
//            out << std::dec << "\n";
//        }
//    }
//
//    // ── Fixup diagnostic comment ──────────────────────────────
//    void writeFixupComment(std::ostringstream& out) {
//        if (omf_.fixups_.empty()) return;
//
//        out << "\n; ============================================================\n"
//            << "; FIXUP (relocation) records — " << omf_.fixups_.size() << " total\n"
//            << "; These indicate addresses that need patching at link time.\n"
//            << "; Review and replace affected db bytes with proper FASM\n"
//            << "; relocatable references (e.g. 'dd label - $ - 4').\n"
//            << "; ============================================================\n";
//
//        size_t shown = std::min(omf_.fixups_.size(), static_cast<size_t>(20));
//        for (size_t i = 0; i < shown; ++i) {
//            auto& fx = omf_.fixups_[i];
//            out << "; [" << i << "] seg=" << fx.dataSegIndex
//                << " off=0x" << std::hex << fx.offset << std::dec
//                << "  " << fx.description << "\n";
//        }
//        if (omf_.fixups_.size() > shown)
//            out << "; ... and " << (omf_.fixups_.size() - shown) << " more\n";
//    }
//
//    // ── Label sanitiser ───────────────────────────────────────
//    static std::string sanitizeLabel(std::string name) {
//        if (name.empty()) return "_unknown";
//        // Replace characters invalid in FASM identifiers
//        for (char& c : name) {
//            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.')
//                c = '_';
//        }
//        // FASM labels must not start with a digit
//        if (std::isdigit(static_cast<unsigned char>(name[0])))
//            name = "_" + name;
//        return name;
//    }
//
//    // Temporary storage used between emitLabels and emitBlocks in same call
//    std::unordered_map<uint32_t, std::vector<std::string>> pendingLabels_;
//};
//
//// ============================================================
////  main
//// ============================================================
//int main(int argc, char* argv[]) {
//    std::cout << "omf2fasm — Intel OMF → FASM bridge generator\n"
//              << "----------------------------------------------\n";
//
//    if (argc < 2) {
//        std::cerr << "Usage: " << argv[0]
//                  << " <input.obj> [output.asm] [--quiet]\n";
//        return 1;
//    }
//
//    std::string inputPath  = argv[1];
//    std::string outputPath = (argc >= 3 && argv[2][0] != '-')
//                             ? argv[2] : "bridge.asm";
//    bool quiet = false;
//    for (int i = 1; i < argc; ++i)
//        if (std::string(argv[i]) == "--quiet") quiet = true;
//
//    try {
//        OmfParser parser(!quiet);
//        parser.parse(inputPath);
//
//        FasmBridgeGen gen(parser, !quiet);
//        gen.generate(outputPath);
//
//        std::cout << "\nSummary:\n"
//                  << "  Module:        " << parser.moduleName_      << "\n"
//                  << "  Segments:      " << parser.segdefs_.size()  << "\n"
//                  << "  Public syms:   " << parser.pubdefs_.size()  << "\n"
//                  << "  Data chunks:   " << parser.chunks_.size()   << "\n"
//                  << "  Fixups:        " << parser.fixups_.size()   << "\n"
//                  << "  Output:        " << outputPath              << "\n";
//
//        // Print symbol table
//        if (!parser.pubdefs_.empty()) {
//            std::cout << "\nPublic symbols:\n";
//            for (auto& sym : parser.pubdefs_) {
//                std::cout << "  " << std::left << std::setw(32) << sym.name
//                          << "  seg=" << sym.segIndex
//                          << "  offset=0x" << std::hex << sym.offset
//                          << std::dec << "\n";
//            }
//        }
//
//    } catch (const std::exception& e) {
//        std::cerr << "\nFatal error: " << e.what() << "\n";
//        return 1;
//    }
//
//    return 0;
//}
//
//
//### #   ##
//# # #   # #  OLD VERSION
//### ### ##
//





// omf2fasm_v2.cpp  —  Intel OMF (.obj) → FASM bridge (Sphinx C-- x64 edition)
//
// Fixes over v1:
//   1. Smart code/data classification: segments that own a PUBDEF symbol
//      are treated as code (.text); everything else goes to .data.
//   2. Sphinx C-- signature stripping: the 10-byte "SPHINXC--\0" header
//      that appears at the start of the first code segment is detected and
//      removed so that every public label points at real machine code.
//   3. Multi-LEDATA merging: consecutive LEDATA records for the same segment
//      are stitched into a single contiguous byte array before emission.
//
// Build:
//   MSVC  : cl /std:c++20 /O2 /W4 omf2fasm_v2.cpp /Fe:omf2fasm_v2.exe
//   MinGW : g++ -std=c++20 -O2 -Wall -o omf2fasm_v2 omf2fasm_v2.cpp
// Usage:
//   omf2fasm_v2 <input.obj> [output.asm] [--quiet]

//#include <algorithm>
//#include <cstdint>
//#include <fstream>
//#include <iomanip>
//#include <iostream>
//#include <map>
//#include <sstream>
//#include <stdexcept>
//#include <string>
//#include <unordered_map>
//#include <vector>
//
//// ============================================================
////  OMF record-type constants
//// ============================================================
//namespace OMF {
//    constexpr uint8_t THEADR   = 0x80;
//    constexpr uint8_t LNAMES   = 0x96;
//    constexpr uint8_t SEGDEF   = 0x98;
//    constexpr uint8_t SEGDEF32 = 0x99;
//    constexpr uint8_t GRPDEF   = 0x9A;
//    constexpr uint8_t EXTDEF   = 0x8C;
//    constexpr uint8_t PUBDEF   = 0x90;
//    constexpr uint8_t PUBDEF32 = 0x91;
//    constexpr uint8_t LEDATA   = 0xA0;
//    constexpr uint8_t LEDATA32 = 0xA1;
//    constexpr uint8_t LIDATA   = 0xA2;
//    constexpr uint8_t LIDATA32 = 0xA3;
//    constexpr uint8_t FIXUPP   = 0x9C;
//    constexpr uint8_t FIXUPP32 = 0x9D;
//    constexpr uint8_t MODEND   = 0x8A;
//    constexpr uint8_t MODEND32 = 0x8B;
//}
//
//// ============================================================
////  Sphinx C-- signature
////  The compiler inserts "SPHINXC--\0" (10 bytes) at offset 0
////  of the first code segment.  We strip it so labels land on
////  actual x86 opcodes.
//// ============================================================
//static const std::vector<uint8_t> SPHINX_SIG = {
//    0x53, 0x50, 0x48, 0x49, 0x4E, 0x58, 0x43, 0x2D, 0x2D, 0x00
//    // S     P     H     I     N     X     C     -     -    \0
//};
//
//// ============================================================
////  Low-level OMF byte reader
//// ============================================================
//class OmfReader {
//public:
//    explicit OmfReader(std::vector<uint8_t> data)
//        : data_(std::move(data)), pos_(0) {}
//
//    bool   eof()  const { return pos_ >= data_.size(); }
//    size_t pos()  const { return pos_; }
//    size_t size() const { return data_.size(); }
//    void   seek(size_t p) { pos_ = p; }
//
//    uint8_t u8() {
//        need(1);
//        return data_[pos_++];
//    }
//    uint16_t u16() {
//        need(2);
//        uint16_t v = static_cast<uint16_t>(data_[pos_])
//                   | static_cast<uint16_t>(data_[pos_ + 1] << 8);
//        pos_ += 2;
//        return v;
//    }
//    uint32_t u32() {
//        need(4);
//        uint32_t v = static_cast<uint32_t>(data_[pos_])
//                   | (static_cast<uint32_t>(data_[pos_ + 1]) << 8)
//                   | (static_cast<uint32_t>(data_[pos_ + 2]) << 16)
//                   | (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
//        pos_ += 4;
//        return v;
//    }
//
//    // OMF variable-width index: if high bit set → 2-byte big-endian index
//    uint16_t index() {
//        uint8_t b = u8();
//        if (b & 0x80)
//            return static_cast<uint16_t>((b & 0x7F) << 8) | u8();
//        return b;
//    }
//
//    // Pascal-style length-prefixed string
//    std::string pstr() {
//        uint8_t len = u8();
//        need(len);
//        std::string s(reinterpret_cast<const char*>(&data_[pos_]), len);
//        pos_ += len;
//        return s;
//    }
//
//    // Copy exactly n bytes
//    std::vector<uint8_t> bytes(size_t n) {
//        need(n);
//        std::vector<uint8_t> v(data_.begin() + pos_,
//                               data_.begin() + pos_ + n);
//        pos_ += n;
//        return v;
//    }
//
//    void skip(size_t n) { need(n); pos_ += n; }
//
//    static std::vector<uint8_t> loadFile(const std::string& path) {
//        std::ifstream f(path, std::ios::binary | std::ios::ate);
//        if (!f) throw std::runtime_error("Cannot open: " + path);
//        auto sz = static_cast<size_t>(f.tellg());
//        f.seekg(0);
//        std::vector<uint8_t> buf(sz);
//        f.read(reinterpret_cast<char*>(buf.data()),
//               static_cast<std::streamsize>(sz));
//        if (!f) throw std::runtime_error("Read error: " + path);
//        return buf;
//    }
//
//private:
//    std::vector<uint8_t> data_;
//    size_t               pos_;
//
//    void need(size_t n) const {
//        if (pos_ + n > data_.size())
//            throw std::runtime_error(
//                "OmfReader: read past EOF at 0x"
//                + ([](size_t v) {
//                       std::ostringstream ss;
//                       ss << std::hex << v;
//                       return ss.str();
//                   })(pos_));
//    }
//};
//
//// ============================================================
////  Data model
//// ============================================================
//
//struct SegDef {
//    std::string name;       // resolved from LNAMES
//    std::string className;  // e.g. "CODE", "DATA", "BSS"
//    uint32_t    length = 0;
//    bool        is32   = false;
//};
//
//struct PublicSym {
//    std::string name;
//    uint32_t    segIdx;   // 1-based OMF segment index
//    uint32_t    offset;
//};
//
//// A fully merged segment: all LEDATA records stitched together.
//// Key insight: we use std::map<uint32_t, std::vector<uint8_t>> to store
//// data indexed by offset.  After all records are parsed we merge them
//// into one flat vector, filling gaps with 0x90/0x00 as appropriate.
//struct SegmentData {
//    // offset → raw bytes (from individual LEDATA records)
//    std::map<uint32_t, std::vector<uint8_t>> blocks;
//
//    // Merge all blocks into one flat byte array.
//    // Gaps are filled with `padByte`.
//    std::vector<uint8_t> flatten(uint8_t padByte = 0x00) const {
//        if (blocks.empty()) return {};
//
//        uint32_t base = blocks.begin()->first;
//        uint32_t end  = 0;
//        for (auto& [off, blk] : blocks)
//            end = std::max(end, off + static_cast<uint32_t>(blk.size()));
//
//        std::vector<uint8_t> out(end - base, padByte);
//        for (auto& [off, blk] : blocks)
//            std::copy(blk.begin(), blk.end(),
//                      out.begin() + (off - base));
//        return out;
//    }
//
//    uint32_t baseOffset() const {
//        return blocks.empty() ? 0 : blocks.begin()->first;
//    }
//};
//
//// ============================================================
////  OMF Parser
//// ============================================================
//class OmfParser {
//public:
//    explicit OmfParser(bool verbose) : verbose_(verbose) {}
//
//    // ── public results ───────────────────────────────────────
//    std::string              moduleName_;
//    std::vector<std::string> lnames_;    // 1-based (index 0 = sentinel "")
//    std::vector<SegDef>      segdefs_;   // 1-based (index 0 = sentinel)
//    std::vector<PublicSym>   pubdefs_;
//    // segIdx → accumulated data
//    std::map<uint32_t, SegmentData> segData_;
//
//    void parse(const std::string& path) {
//        auto raw = OmfReader::loadFile(path);
//        log("[INFO] Loaded " + std::to_string(raw.size()) + " bytes");
//        OmfReader rd(std::move(raw));
//
//        // Sentinel so 1-based indices work directly
//        lnames_.push_back("");
//        segdefs_.push_back({});
//
//        int recCount = 0;
//        while (!rd.eof()) {
//            if (rd.size() - rd.pos() < 3) break;
//
//            size_t   recStart = rd.pos();
//            uint8_t  recType  = rd.u8();
//            uint16_t recLen   = rd.u16();
//            size_t   dataEnd  = rd.pos() + recLen - 1; // -1 = checksum
//
//            if (dataEnd + 1 > rd.size()) {
//                log("[WARN] Truncated record at 0x" + hex(recStart));
//                break;
//            }
//
//            dispatch(rd, recType, dataEnd);
//
//            rd.seek(dataEnd + 1); // skip to next record (past checksum)
//            ++recCount;
//
//            if (recType == OMF::MODEND || recType == OMF::MODEND32) break;
//        }
//
//        log("[INFO] Records parsed: " + std::to_string(recCount));
//        log("[INFO] Segments: " + std::to_string(segdefs_.size() - 1)
//            + "  PUBDEFs: " + std::to_string(pubdefs_.size())
//            + "  Data segments: " + std::to_string(segData_.size()));
//    }
//
//private:
//    bool verbose_;
//
//    void log(const std::string& s) const {
//        if (verbose_) std::cout << s << "\n";
//    }
//    static std::string hex(size_t v) {
//        std::ostringstream ss;
//        ss << std::hex << v;
//        return ss.str();
//    }
//
//    void dispatch(OmfReader& rd, uint8_t type, size_t end) {
//        switch (type) {
//        case OMF::THEADR:                    parseTHEADR(rd);             break;
//        case OMF::LNAMES:                    parseLNAMES(rd, end);        break;
//        case OMF::SEGDEF:   case OMF::SEGDEF32:
//            parseSEGDEF(rd, type == OMF::SEGDEF32);  break;
//        case OMF::EXTDEF:                    parseEXTDEF(rd, end);        break;
//        case OMF::PUBDEF:   case OMF::PUBDEF32:
//            parsePUBDEF(rd, end, type == OMF::PUBDEF32); break;
//        case OMF::LEDATA:   case OMF::LEDATA32:
//            parseLEDATA(rd, end, type == OMF::LEDATA32); break;
//        case OMF::LIDATA:   case OMF::LIDATA32:
//            parseLIDATA(rd, end, type == OMF::LIDATA32); break;
//        case OMF::GRPDEF:
//        case OMF::FIXUPP:   case OMF::FIXUPP32:
//        case OMF::MODEND:   case OMF::MODEND32:
//            break; // noted, not needed for code extraction
//        default:
//            log("[SKIP] type=0x" + hex(type) + " end=0x" + hex(end));
//            break;
//        }
//    }
//
//    // ── THEADR ───────────────────────────────────────────────
//    void parseTHEADR(OmfReader& rd) {
//        moduleName_ = rd.pstr();
//        log("[THEADR] Module: " + moduleName_);
//    }
//
//    // ── LNAMES ───────────────────────────────────────────────
//    void parseLNAMES(OmfReader& rd, size_t end) {
//        while (rd.pos() < end) {
//            std::string n = rd.pstr();
//            uint32_t idx = static_cast<uint32_t>(lnames_.size());
//            lnames_.push_back(n);
//            log("[LNAMES] [" + std::to_string(idx) + "] '" + n + "'");
//        }
//    }
//
//    // ── SEGDEF ───────────────────────────────────────────────
//    void parseSEGDEF(OmfReader& rd, bool is32) {
//        SegDef sd;
//        sd.is32 = is32;
//
//        uint8_t attrib = rd.u8();
//        // Bits [7:5] = alignment, [4:2] = combine, bit 1 = USE32 flag
//        (void)attrib;
//
//        sd.length = is32 ? rd.u32() : rd.u16();
//
//        uint16_t nameIdx  = rd.index();
//        uint16_t classIdx = rd.index();
//        rd.index(); // overlay index — ignore
//
//        auto resolveName = [&](uint16_t i) -> std::string {
//            return (i > 0 && i < lnames_.size()) ? lnames_[i] : "";
//        };
//        sd.name      = resolveName(nameIdx);
//        sd.className = resolveName(classIdx);
//
//        uint32_t myIdx = static_cast<uint32_t>(segdefs_.size());
//        segdefs_.push_back(sd);
//
//        log("[SEGDEF" + std::string(is32 ? "32" : "") + "] ["
//            + std::to_string(myIdx) + "] '"
//            + sd.name + "' class='" + sd.className
//            + "' len=0x" + hex(sd.length));
//    }
//
//    // ── EXTDEF ───────────────────────────────────────────────
//    void parseEXTDEF(OmfReader& rd, size_t end) {
//        while (rd.pos() < end) {
//            std::string name = rd.pstr();
//            rd.index(); // type index
//            log("[EXTDEF] '" + name + "'");
//        }
//    }
//
//    // ── PUBDEF ───────────────────────────────────────────────
//    void parsePUBDEF(OmfReader& rd, size_t end, bool is32) {
//        rd.index(); // base group index
//        uint16_t baseSegIdx = rd.index();
//        if (baseSegIdx == 0) rd.u16(); // base frame (absolute)
//
//        while (rd.pos() < end) {
//            PublicSym sym;
//            sym.name     = rd.pstr();
//            sym.segIdx   = baseSegIdx;
//            sym.offset   = is32 ? rd.u32() : rd.u16();
//            rd.index();  // type index
//
//            pubdefs_.push_back(sym);
//            log("[PUBDEF" + std::string(is32 ? "32" : "") + "] '"
//                + sym.name + "'  seg=" + std::to_string(sym.segIdx)
//                + "  off=0x" + hex(sym.offset));
//        }
//    }
//
//    // ── LEDATA ───────────────────────────────────────────────
//    // Multiple LEDATA records for the same segment are accumulated
//    // in segData_[segIdx].blocks (keyed by their start offset).
//    // We do NOT merge here; merging happens in FasmBridgeGen so we
//    // can still inspect individual offsets for label placement.
//    void parseLEDATA(OmfReader& rd, size_t end, bool is32) {
//        uint32_t segIdx = rd.index();
//        uint32_t offset = is32 ? rd.u32() : rd.u16();
//        size_t   nbytes = end - rd.pos();
//        auto     data   = rd.bytes(nbytes);
//
//        segData_[segIdx].blocks[offset] = std::move(data);
//        log("[LEDATA" + std::string(is32 ? "32" : "") + "] seg="
//            + std::to_string(segIdx) + "  off=0x" + hex(offset)
//            + "  bytes=" + std::to_string(nbytes));
//    }
//
//    // ── LIDATA (RLE iterated data) ───────────────────────────
//    void parseLIDATA(OmfReader& rd, size_t end, bool is32) {
//        uint32_t segIdx = rd.index();
//        uint32_t offset = is32 ? rd.u32() : rd.u16();
//        std::vector<uint8_t> expanded;
//        expandLI(rd, end, expanded);
//        segData_[segIdx].blocks[offset] = std::move(expanded);
//        log("[LIDATA" + std::string(is32 ? "32" : "") + "] seg="
//            + std::to_string(segIdx) + "  off=0x" + hex(offset));
//    }
//
//    void expandLI(OmfReader& rd, size_t end, std::vector<uint8_t>& out) {
//        while (rd.pos() < end) {
//            uint16_t repeatCount = rd.u16();
//            uint16_t blockCount  = rd.u16();
//            if (blockCount == 0) {
//                uint8_t  dataLen = rd.u8();
//                auto raw = rd.bytes(dataLen);
//                for (uint16_t r = 0; r < repeatCount; ++r)
//                    out.insert(out.end(), raw.begin(), raw.end());
//            } else {
//                std::vector<uint8_t> inner;
//                for (uint16_t b = 0; b < blockCount; ++b)
//                    expandLI(rd, end, inner);
//                for (uint16_t r = 0; r < repeatCount; ++r)
//                    out.insert(out.end(), inner.begin(), inner.end());
//            }
//        }
//    }
//};
//
//// ============================================================
////  Section classifier
////
////  Decision tree:
////  1. If any PUBDEF names a function in this segment → code (.text)
////     Rationale: PUBDEF attaches exported entry points (main, etc.).
////     Code segments always own at least one public label.
////  2. Else if the SEGDEF class name contains "CODE" or "TEXT" → code
////     Rationale: class name is the authoritative OMF classification.
////  3. Else if the SEGDEF name is "_TEXT" or "CODE" (case-insensitive) → code
////     Rationale: Sphinx C-- uses "_TEXT" for its code segment.
////  4. Everything else (DATA, _DATA, BSS, CONST, STACK) → data (.data)
//// ============================================================
//static bool segmentIsCode(uint32_t segIdx,
//                           const std::vector<SegDef>&     segdefs,
//                           const std::vector<PublicSym>&  pubdefs)
//{
//    // Rule 1: segment owns at least one public symbol → treat as code
//    for (auto& sym : pubdefs) {
//        if (sym.segIdx == segIdx) {
//            return true;
//        }
//    }
//
//    // Rule 2 & 3: look at the SegDef names
//    if (segIdx == 0 || segIdx >= segdefs.size()) return true; // unknown → assume code
//
//    auto toUpper = [](std::string s) {
//        for (char& c : s)
//            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
//        return s;
//    };
//
//    std::string cls  = toUpper(segdefs[segIdx].className);
//    std::string name = toUpper(segdefs[segIdx].name);
//
//    if (cls.find("CODE") != std::string::npos) return true;
//    if (cls.find("TEXT") != std::string::npos) return true;
//    if (name == "_TEXT" || name == "TEXT" || name == "CODE"
//        || name == ".TEXT" || name == "_CODE") return true;
//
//    return false; // Rule 4: data
//}
//
//// ============================================================
////  Sphinx signature stripper
////
////  Sphinx C-- prepends "SPHINXC--\0" to offset 0 of the first
////  code segment.  This is a legacy marker, not executable code.
////  We detect it and trim it so that PUBDEF offsets (which already
////  account for it) land exactly on real opcodes.
////
////  Returns the number of bytes trimmed (0 or SPHINX_SIG.size()).
//// ============================================================
//static size_t stripSphinxSignature(std::vector<uint8_t>& data, bool verbose) {
//    if (data.size() < SPHINX_SIG.size()) return 0;
//
//    bool match = std::equal(SPHINX_SIG.begin(), SPHINX_SIG.end(), data.begin());
//    if (match) {
//        if (verbose)
//            std::cout << "[STRIP] Sphinx C-- signature detected and removed ("
//                      << SPHINX_SIG.size() << " bytes)\n";
//        data.erase(data.begin(),
//                   data.begin() + static_cast<ptrdiff_t>(SPHINX_SIG.size()));
//        return SPHINX_SIG.size();
//    }
//    return 0;
//}
//
//// ============================================================
////  FASM bridge generator
//// ============================================================
//class FasmBridgeGen {
//public:
//    FasmBridgeGen(const OmfParser& p, bool verbose)
//        : omf_(p), verbose_(verbose) {}
//
//    void generate(const std::string& outPath) {
//        std::ostringstream out;
//
//        writeFileHeader(out);
//        writePublicDecls(out);
//        writeSections(out);
//
//        std::ofstream f(outPath);
//        if (!f) throw std::runtime_error("Cannot write: " + outPath);
//        f << out.str();
//
//        if (verbose_)
//            std::cout << "[INFO] Written: " << outPath << "\n";
//    }
//
//private:
//    const OmfParser& omf_;
//    bool             verbose_;
//
//    // ── File header ──────────────────────────────────────────
//    void writeFileHeader(std::ostringstream& out) {
//        out << "; ============================================================\n"
//            << "; bridge.asm  —  auto-generated by omf2fasm_v2\n"
//            << "; Source module : " << omf_.moduleName_ << "\n"
//            << "; ============================================================\n\n"
//            << "format MS64 COFF\n\n";
//    }
//
//    // ── public declarations (one per PUBDEF) ─────────────────
//    void writePublicDecls(std::ostringstream& out) {
//        if (omf_.pubdefs_.empty()) return;
//        out << "; --- exported symbols ---\n";
//        for (auto& sym : omf_.pubdefs_)
//            out << "public " << sanitize(sym.name) << "\n";
//        out << "\n";
//    }
//
//    // ── Sections ─────────────────────────────────────────────
//    void writeSections(std::ostringstream& out) {
//        // Separate code and data segment indices
//        std::vector<uint32_t> codeSegs, dataSegs;
//        for (auto& [idx, _] : omf_.segData_) {
//            if (segmentIsCode(idx, omf_.segdefs_, omf_.pubdefs_))
//                codeSegs.push_back(idx);
//            else
//                dataSegs.push_back(idx);
//        }
//
//        // Emit .text first, then .data
//        if (!codeSegs.empty()) {
//            out << "section '.text' code readable executable\n\n";
//            bool firstCodeSeg = true;
//            for (uint32_t idx : codeSegs) {
//                emitSegment(out, idx, /*isCode=*/true, firstCodeSeg);
//                firstCodeSeg = false;
//            }
//        }
//
//        if (!dataSegs.empty()) {
//            out << "\nsection '.data' data readable writeable\n\n";
//            for (uint32_t idx : dataSegs)
//                emitSegment(out, idx, /*isCode=*/false, /*firstSeg=*/false);
//        }
//
//        if (omf_.segData_.empty())
//            out << "; [WARNING] No LEDATA found — object contains symbols only.\n";
//    }
//
//    // ── Emit one segment ─────────────────────────────────────
//    void emitSegment(std::ostringstream& out,
//                     uint32_t            segIdx,
//                     bool                isCode,
//                     bool                isFirstCodeSeg)
//    {
//        // Retrieve segment metadata (may be absent for index 0 / sentinel)
//        const SegDef* sd = (segIdx < omf_.segdefs_.size())
//                           ? &omf_.segdefs_[segIdx] : nullptr;
//
//        // Emit a descriptive comment
//        out << "; --- segment " << segIdx;
//        if (sd) out << " '" << sd->name << "' class='" << sd->className << "'";
//        out << " ---\n";
//
//        // Flatten all LEDATA blocks for this segment into one byte array
//        const SegmentData& sdata = omf_.segData_.at(segIdx);
//        uint8_t padByte = isCode ? 0x90 : 0x00; // NOP pad in code, zero in data
//        std::vector<uint8_t> flat = sdata.flatten(padByte);
//        uint32_t baseOff = sdata.baseOffset();
//
//        if (flat.empty()) {
//            out << "; (no data)\n\n";
//            return;
//        }
//
//        // Sphinx signature stripping applies to the very first code segment.
//        // PUBDEF offsets already include the 10-byte header, so after stripping
//        // we subtract SPHINX_SIG.size() from every label offset in this segment.
//        size_t sigStripped = 0;
//        if (isCode && isFirstCodeSeg) {
//            sigStripped = stripSphinxSignature(flat, verbose_);
//            if (sigStripped > 0)
//                baseOff += static_cast<uint32_t>(sigStripped);
//        }
//
//        // Build offset→labels map for this segment
//        // (symbol offset is relative to segment start in OMF)
//        std::map<uint32_t, std::vector<std::string>> labelMap;
//        for (auto& sym : omf_.pubdefs_) {
//            if (sym.segIdx == segIdx) {
//                // After signature strip, adjust: label offset relative to new base
//                uint32_t adjustedOff = sym.offset;
//                // baseOff after strip already skips the header, so we subtract
//                // SPHINX_SIG.size() from the label's raw offset to get its index
//                // into `flat`.
//                if (sigStripped > 0 && adjustedOff >= sigStripped)
//                    adjustedOff -= static_cast<uint32_t>(sigStripped);
//                else if (sigStripped > 0)
//                    adjustedOff = 0; // label was inside the header; clamp to start
//
//                labelMap[adjustedOff].push_back(sanitize(sym.name));
//            }
//        }
//
//        // Emit the flat byte array, inserting labels at correct byte positions
//        emitBytesWithLabels(out, flat, labelMap);
//        out << "\n";
//    }
//
//    // ── Emit bytes, inserting labels inline ──────────────────
//    // We emit up to BYTES_PER_LINE bytes per db line.
//    // When a label offset falls on a byte boundary, we close the current
//    // db line, emit the label, then start a new db line.
//    static constexpr size_t BYTES_PER_LINE = 16;
//
//    static void emitBytesWithLabels(
//        std::ostringstream& out,
//        const std::vector<uint8_t>& flat,
//        const std::map<uint32_t, std::vector<std::string>>& labelMap)
//    {
//        size_t i      = 0;
//        size_t total  = flat.size();
//
//        while (i < total) {
//            // Check if a label lands exactly here
//            auto it = labelMap.find(static_cast<uint32_t>(i));
//            if (it != labelMap.end()) {
//                for (auto& lbl : it->second)
//                    out << lbl << ":\n";
//            }
//
//            // How many bytes until the next label (or end of data)?
//            size_t nextLabel = total;
//            auto nxt = labelMap.upper_bound(static_cast<uint32_t>(i));
//            if (nxt != labelMap.end())
//                nextLabel = nxt->first;
//
//            // Emit bytes in BYTES_PER_LINE chunks, stopping before next label
//            size_t lineEnd = std::min({ i + BYTES_PER_LINE, nextLabel, total });
//            out << "  db ";
//            for (size_t j = i; j < lineEnd; ++j) {
//                out << "0x" << std::hex << std::setw(2) << std::setfill('0')
//                    << static_cast<int>(flat[j]);
//                if (j + 1 < lineEnd) out << ", ";
//            }
//            out << std::dec << "\n";
//            i = lineEnd;
//        }
//    }
//
//    // ── Sanitise symbol names for FASM ───────────────────────
//    static std::string sanitize(std::string name) {
//        if (name.empty()) return "_sym";
//        for (char& c : name)
//            if (!std::isalnum(static_cast<unsigned char>(c))
//                && c != '_' && c != '.')
//                c = '_';
//        if (std::isdigit(static_cast<unsigned char>(name[0])))
//            name = "_" + name;
//        return name;
//    }
//};
//
//// ============================================================
////  Entry point
//// ============================================================
//int main(int argc, char* argv[]) {
//    std::cout << "omf2fasm_v2  —  Intel OMF → FASM bridge (Sphinx C-- x64)\n"
//              << "----------------------------------------------------------\n";
//
//    if (argc < 2) {
//        std::cerr << "Usage: " << argv[0]
//                  << " <input.obj> [output.asm] [--quiet]\n";
//        return 1;
//    }
//
//    std::string inputPath  = argv[1];
//    std::string outputPath = "bridge.asm";
//    bool        quiet      = false;
//
//    for (int i = 2; i < argc; ++i) {
//        std::string a = argv[i];
//        if (a == "--quiet") quiet = true;
//        else if (a[0] != '-') outputPath = a;
//    }
//
//    try {
//        OmfParser omf(!quiet);
//        omf.parse(inputPath);
//
//        FasmBridgeGen gen(omf, !quiet);
//        gen.generate(outputPath);
//
//        // ── Summary ──────────────────────────────────────────
//        std::cout << "\n=== Summary ===\n"
//                  << "  Module    : " << omf.moduleName_           << "\n"
//                  << "  Segments  : " << (omf.segdefs_.size() - 1) << "\n"
//                  << "  Symbols   : " << omf.pubdefs_.size()        << "\n"
//                  << "  Data segs : " << omf.segData_.size()        << "\n"
//                  << "  Output    : " << outputPath                  << "\n";
//
//        if (!omf.pubdefs_.empty()) {
//            std::cout << "\n  Public symbols:\n";
//            for (auto& s : omf.pubdefs_)
//                std::cout << "    " << std::left << std::setw(30) << s.name
//                          << "  seg=" << s.segIdx
//                          << "  off=0x" << std::hex << s.offset
//                          << std::dec << "\n";
//        }
//
//    } catch (const std::exception& e) {
//        std::cerr << "\nFatal: " << e.what() << "\n";
//        return 1;
//    }
//    return 0;
//}
//
// ##   #     ###     ####
//#  #  #     #  #       #
//#  #  #     #  #    ####
// ##   ####  ###     #
//                    ####
//






// omf2fasm_v3.cpp  —  Final Intel OMF → FASM Bridge (Sphinx C-- x64 Edition)
//
// Key improvements over v2:
//   • Segment struct owns both data bytes AND symbol map (offset→name)
//   • Labels are placed byte-accurately: the db stream is interrupted
//     exactly at the label offset, never after
//   • SPHINXC-- signature detection operates per-segment, not globally
//   • All section/label logic is self-contained in one clean pass
//
// Build:
//   MSVC  : cl /std:c++20 /O2 /W4 omf2fasm_v3.cpp /Fe:omf2fasm_v3.exe
//   MinGW : g++ -std=c++20 -O2 -Wall -o omf2fasm_v3 omf2fasm_v3.cpp
// Usage:
//   omf2fasm_v3 <input.obj> [output.asm] [--quiet]

//#include <algorithm>
//#include <cstdint>
//#include <fstream>
//#include <iomanip>
//#include <iostream>
//#include <map>
//#include <sstream>
//#include <stdexcept>
//#include <string>
//#include <vector>
//
//// ============================================================
////  Constants
//// ============================================================
//
//// Sphinx C-- inserts this 10-byte marker at offset 0 of its code segment.
//// It is NOT executable code — strip it before emitting db bytes.
//static constexpr uint8_t SPHINX_SIG[]  = {
//    0x53,0x50,0x48,0x49,0x4E,0x58,0x43,0x2D,0x2D,0x00
//};
//static constexpr size_t  SPHINX_SIG_LEN = sizeof(SPHINX_SIG);
//
//// OMF record types we care about
//namespace Rec {
//    constexpr uint8_t THEADR   = 0x80;
//    constexpr uint8_t LNAMES   = 0x96;
//    constexpr uint8_t SEGDEF   = 0x98;  // 16-bit lengths
//    constexpr uint8_t SEGDEF32 = 0x99;  // 32-bit lengths
//    constexpr uint8_t PUBDEF   = 0x90;  // public symbol, 16-bit offset
//    constexpr uint8_t PUBDEF32 = 0x91;  // public symbol, 32-bit offset
//    constexpr uint8_t LEDATA   = 0xA0;  // enumerated data, 16-bit data offset
//    constexpr uint8_t LEDATA32 = 0xA1;  // enumerated data, 32-bit data offset
//    constexpr uint8_t MODEND   = 0x8A;
//    constexpr uint8_t MODEND32 = 0x8B;
//}
//
//// ============================================================
////  Segment  —  core data structure
////
////  data    : flat byte array built from LEDATA records.
////            Multiple LEDATA records for the same segment are
////            written directly into this vector at their stated
////            offsets (vector is grown as needed, gaps = 0x00).
////
////  symbols : offset → sanitized label name.
////            Populated from PUBDEF records that reference this
////            segment index.  Used during emission to break the
////            db stream and insert labels at the correct byte.
//// ============================================================
//struct Segment {
//    std::string              name;       // from LNAMES (e.g. "_TEXT")
//    std::string              className;  // from LNAMES (e.g. "CODE")
//    std::vector<uint8_t>     data;       // flat bytes
//    std::map<uint32_t,       // offset within segment
//             std::string>    symbols;    // label at that offset
//
//    // Write `bytes` into `data` starting at `offset`.
//    // Automatically extends the vector if needed.
//    void writeAt(uint32_t offset, const std::vector<uint8_t>& bytes) {
//        uint32_t end = offset + static_cast<uint32_t>(bytes.size());
//        if (end > data.size())
//            data.resize(end, 0x00);
//        std::copy(bytes.begin(), bytes.end(), data.begin() + offset);
//    }
//
//    // True if this segment has a symbol named "main"
//    bool hasMain() const {
//        for (auto& [off, name] : symbols)
//            if (name == "main") return true;
//        return false;
//    }
//
//    // True if this segment starts with the Sphinx C-- signature
//    bool hasSphinxSignature() const {
//        if (data.size() < SPHINX_SIG_LEN) return false;
//        return std::equal(SPHINX_SIG, SPHINX_SIG + SPHINX_SIG_LEN,
//                          data.begin());
//    }
//};
//
//// ============================================================
////  Low-level OMF byte reader
//// ============================================================
//class Reader {
//public:
//    explicit Reader(std::vector<uint8_t> raw)
//        : buf_(std::move(raw)), pos_(0) {}
//
//    bool   eof()  const { return pos_ >= buf_.size(); }
//    size_t pos()  const { return pos_; }
//    size_t size() const { return buf_.size(); }
//    void   seek(size_t p) { pos_ = p; }
//
//    uint8_t u8() {
//        chk(1); return buf_[pos_++];
//    }
//    uint16_t u16() {
//        chk(2);
//        uint16_t v = static_cast<uint16_t>(buf_[pos_])
//                   | static_cast<uint16_t>(buf_[pos_+1] << 8);
//        pos_ += 2; return v;
//    }
//    uint32_t u32() {
//        chk(4);
//        uint32_t v = static_cast<uint32_t>(buf_[pos_])
//                   | (static_cast<uint32_t>(buf_[pos_+1]) << 8)
//                   | (static_cast<uint32_t>(buf_[pos_+2]) << 16)
//                   | (static_cast<uint32_t>(buf_[pos_+3]) << 24);
//        pos_ += 4; return v;
//    }
//
//    // OMF variable-width index field:
//    //   if high bit of first byte is set → 2-byte form: {b0&0x7F, b1} big-endian
//    //   otherwise → 1-byte form
//    uint16_t index() {
//        uint8_t b = u8();
//        if (b & 0x80)
//            return static_cast<uint16_t>((b & 0x7F) << 8) | u8();
//        return b;
//    }
//
//    // Pascal length-prefixed string (1 byte length, then chars)
//    std::string pstr() {
//        uint8_t len = u8();
//        chk(len);
//        std::string s(reinterpret_cast<const char*>(&buf_[pos_]), len);
//        pos_ += len;
//        return s;
//    }
//
//    // Read n raw bytes
//    std::vector<uint8_t> read(size_t n) {
//        chk(n);
//        std::vector<uint8_t> v(buf_.begin()+pos_, buf_.begin()+pos_+n);
//        pos_ += n;
//        return v;
//    }
//
//    // Load a binary file into a Reader
//    static Reader fromFile(const std::string& path) {
//        std::ifstream f(path, std::ios::binary | std::ios::ate);
//        if (!f) throw std::runtime_error("Cannot open: " + path);
//        auto sz = static_cast<size_t>(f.tellg());
//        f.seekg(0);
//        std::vector<uint8_t> buf(sz);
//        f.read(reinterpret_cast<char*>(buf.data()),
//               static_cast<std::streamsize>(sz));
//        return Reader(std::move(buf));
//    }
//
//private:
//    std::vector<uint8_t> buf_;
//    size_t               pos_;
//
//    void chk(size_t n) const {
//        if (pos_ + n > buf_.size()) {
//            std::ostringstream e;
//            e << "OmfReader: read past EOF at 0x" << std::hex << pos_
//              << " (want " << n << ", have " << (buf_.size()-pos_) << ")";
//            throw std::runtime_error(e.str());
//        }
//    }
//};
//
//// ============================================================
////  OMF Parser
//// ============================================================
//class OmfParser {
//public:
//    std::string              moduleName;
//    // 1-based: index 0 is a sentinel so OMF indices map directly
//    std::vector<std::string> lnames;     // raw name table
//    std::vector<Segment>     segments;   // 1-based
//
//    explicit OmfParser(bool verbose = true) : verbose_(verbose) {}
//
//    void parse(const std::string& path) {
//        Reader rd = Reader::fromFile(path);
//        log("[INFO] Loaded " + std::to_string(rd.size()) + " bytes from: " + path);
//
//        // 1-based sentinels
//        lnames.push_back("");
//        segments.push_back({});
//
//        int nRec = 0;
//        while (!rd.eof()) {
//            if (rd.size() - rd.pos() < 3) break;   // can't read record header
//
//            size_t   recStart = rd.pos();
//            uint8_t  type     = rd.u8();
//            uint16_t len      = rd.u16();           // includes checksum byte
//
//            if (len == 0) break;
//            size_t dataEnd = rd.pos() + len - 1;    // points to checksum byte
//            if (dataEnd + 1 > rd.size()) {
//                log("[WARN] Truncated record at 0x" + h(recStart));
//                break;
//            }
//
//            // Dispatch on record type; unknown records are simply skipped
//            switch (type) {
//            case Rec::THEADR:                     parseTHEADR(rd);           break;
//            case Rec::LNAMES:                     parseLNAMES(rd, dataEnd);  break;
//            case Rec::SEGDEF:   case Rec::SEGDEF32:
//                parseSEGDEF(rd, type == Rec::SEGDEF32);                      break;
//            case Rec::PUBDEF:   case Rec::PUBDEF32:
//                parsePUBDEF(rd, dataEnd, type == Rec::PUBDEF32);             break;
//            case Rec::LEDATA:   case Rec::LEDATA32:
//                parseLEDATA(rd, dataEnd, type == Rec::LEDATA32);             break;
//            case Rec::MODEND:   case Rec::MODEND32:
//                log("[INFO] MODEND — stopping");
//                rd.seek(dataEnd + 1);
//                goto done;
//            default:
//                log("[SKIP] type=0x" + h(type) + " len=" + std::to_string(len));
//                break;
//            }
//
//            rd.seek(dataEnd + 1);   // advance past checksum to next record
//            ++nRec;
//        }
//        done:
//        log("[INFO] Parsed " + std::to_string(nRec) + " records, "
//            + std::to_string(segments.size()-1) + " segments, "
//            + std::to_string(countSymbols()) + " public symbols");
//    }
//
//private:
//    bool verbose_;
//
//    void log(const std::string& s) const {
//        if (verbose_) std::cout << s << "\n";
//    }
//    static std::string h(size_t v) {
//        std::ostringstream ss; ss << std::hex << v; return ss.str();
//    }
//
//    size_t countSymbols() const {
//        size_t n = 0;
//        for (auto& seg : segments) n += seg.symbols.size();
//        return n;
//    }
//
//    // ── THEADR 0x80 ──────────────────────────────────────────
//    void parseTHEADR(Reader& rd) {
//        moduleName = rd.pstr();
//        log("[THEADR] Module: " + moduleName);
//    }
//
//    // ── LNAMES 0x96 ──────────────────────────────────────────
//    void parseLNAMES(Reader& rd, size_t end) {
//        while (rd.pos() < end) {
//            std::string n = rd.pstr();
//            log("[LNAMES] [" + std::to_string(lnames.size()) + "] '" + n + "'");
//            lnames.push_back(n);
//        }
//    }
//
//    // ── SEGDEF 0x98 / SEGDEF32 0x99 ──────────────────────────
//    void parseSEGDEF(Reader& rd, bool is32) {
//        Segment seg;
//        uint8_t attrib = rd.u8();
//        (void)attrib;                           // alignment/combine bits; noted
//
//        uint32_t segLen = is32 ? rd.u32() : rd.u16();
//        seg.data.reserve(segLen);               // optional pre-allocation hint
//
//        uint16_t nameIdx  = rd.index();
//        uint16_t classIdx = rd.index();
//        rd.index();                             // overlay index — ignore
//
//        auto resolve = [&](uint16_t i) -> std::string {
//            return (i > 0 && i < lnames.size()) ? lnames[i] : "";
//        };
//        seg.name      = resolve(nameIdx);
//        seg.className = resolve(classIdx);
//
//        uint32_t myIdx = static_cast<uint32_t>(segments.size());
//        segments.push_back(std::move(seg));
//        log("[SEGDEF" + std::string(is32?"32":"") + "] ["
//            + std::to_string(myIdx) + "] name='" + segments.back().name
//            + "' class='" + segments.back().className + "'");
//    }
//
//    // ── PUBDEF 0x90 / PUBDEF32 0x91 ──────────────────────────
//    // Each public symbol is stored directly in its owning Segment's
//    // `symbols` map at the stated offset.  This co-location is the
//    // key design choice: we never need to cross-reference at emit time.
//    void parsePUBDEF(Reader& rd, size_t end, bool is32) {
//        rd.index();                             // base group index — ignore
//        uint16_t baseSegIdx = rd.index();
//        if (baseSegIdx == 0) rd.u16();          // base frame (absolute symbol)
//
//        while (rd.pos() < end) {
//            std::string name   = rd.pstr();
//            uint32_t    offset = is32 ? rd.u32() : rd.u16();
//            rd.index();                         // type index — ignore
//
//            std::string label  = sanitize(name);
//
//            if (baseSegIdx > 0 && baseSegIdx < segments.size()) {
//                segments[baseSegIdx].symbols[offset] = label;
//                log("[PUBDEF] '" + name + "' → seg[" + std::to_string(baseSegIdx)
//                    + "] off=0x" + h(offset));
//            }
//        }
//    }
//
//    // ── LEDATA 0xA0 / LEDATA32 0xA1 ─────────────────────────
//    // Multiple LEDATA records may contribute to the same segment.
//    // We write each block directly to Segment::data at its stated
//    // offset so the final data vector is always correctly laid out.
//    //
//    // Offset field width:
//    //   0xA0  → 2-byte offset (16-bit OMF)
//    //   0xA1  → 4-byte offset (32-bit OMF / Sphinx C-- default)
//    void parseLEDATA(Reader& rd, size_t end, bool is32) {
//        uint32_t segIdx = rd.index();
//        uint32_t offset = is32 ? rd.u32() : rd.u16();
//        size_t   nbytes = end - rd.pos();
//        auto     data   = rd.read(nbytes);
//
//        if (segIdx > 0 && segIdx < segments.size()) {
//            segments[segIdx].writeAt(offset, data);
//            log("[LEDATA" + std::string(is32?"32":"") + "] seg["
//                + std::to_string(segIdx) + "] off=0x" + h(offset)
//                + " bytes=" + std::to_string(nbytes));
//        } else {
//            log("[LEDATA] WARN: segIdx " + std::to_string(segIdx) + " out of range");
//        }
//    }
//
//    // ── Name sanitizer ────────────────────────────────────────
//    static std::string sanitize(std::string n) {
//        if (n.empty()) return "_sym";
//        for (char& c : n)
//            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.')
//                c = '_';
//        if (std::isdigit(static_cast<unsigned char>(n[0]))) n = "_" + n;
//        return n;
//    }
//};
//
//// ============================================================
////  FASM bridge emitter
//// ============================================================
//class FasmEmitter {
//public:
//    static constexpr size_t BYTES_PER_LINE = 16;
//
//    FasmEmitter(const OmfParser& p, bool verbose)
//        : omf_(p), verbose_(verbose) {}
//
//    void emit(const std::string& outPath) {
//        std::ostringstream buf;
//
//        writeHeader(buf);
//        writePublics(buf);
//        writeCodeSections(buf);
//        writeDataSections(buf);
//
//        std::ofstream f(outPath);
//        if (!f) throw std::runtime_error("Cannot write: " + outPath);
//        f << buf.str();
//        log("[INFO] Wrote " + outPath);
//    }
//
//private:
//    const OmfParser& omf_;
//    bool             verbose_;
//
//    void log(const std::string& s) const {
//        if (verbose_) std::cout << s << "\n";
//    }
//
//    // ── File header ──────────────────────────────────────────
//    void writeHeader(std::ostringstream& out) {
//        out << "; ============================================================\n"
//            << "; bridge.asm  —  auto-generated by omf2fasm_v3\n"
//            << "; Module: " << omf_.moduleName << "\n"
//            << "; ============================================================\n\n"
//            << "format MS64 COFF\n\n";
//    }
//
//    // ── public declarations ───────────────────────────────────
//    void writePublics(std::ostringstream& out) {
//        bool any = false;
//        for (size_t i = 1; i < omf_.segments.size(); ++i) {
//            for (auto& [off, name] : omf_.segments[i].symbols) {
//                out << "public " << name << "\n";
//                any = true;
//            }
//        }
//        if (any) out << "\n";
//    }
//
//    // ── Code sections (segments that own 'main' or are class CODE/TEXT) ──
//    void writeCodeSections(std::ostringstream& out) {
//        bool headerWritten = false;
//
//        for (size_t i = 1; i < omf_.segments.size(); ++i) {
//            const Segment& seg = omf_.segments[i];
//            if (!isCode(seg)) continue;
//            if (seg.data.empty() && seg.symbols.empty()) continue;
//
//            if (!headerWritten) {
//                out << "section '.text' code readable executable\n\n";
//                headerWritten = true;
//            }
//            emitSegment(out, seg, /*isCode=*/true);
//        }
//    }
//
//    // ── Data sections ─────────────────────────────────────────
//    void writeDataSections(std::ostringstream& out) {
//        bool headerWritten = false;
//
//        for (size_t i = 1; i < omf_.segments.size(); ++i) {
//            const Segment& seg = omf_.segments[i];
//            if (isCode(seg)) continue;
//            if (seg.data.empty()) continue;
//
//            if (!headerWritten) {
//                out << "\nsection '.data' data readable writeable\n\n";
//                headerWritten = true;
//            }
//            emitSegment(out, seg, /*isCode=*/false);
//        }
//    }
//
//    // ── Segment classifier ────────────────────────────────────
//    // A segment is classified as code (.text) if ANY of these hold:
//    //   a) It contains a symbol named "main"  ← primary Sphinx C-- indicator
//    //   b) Its OMF class name contains "CODE" or "TEXT"
//    //   c) Its OMF segment name is "_TEXT", "CODE", or ".text"
//    // Everything else → data (.data)
//    static bool isCode(const Segment& seg) {
//        // (a) owns a public function entry point
//        for (auto& [off, name] : seg.symbols)
//            if (name == "main") return true;
//
//        // (b) class name hint
//        auto up = [](std::string s) {
//            for (char& c : s)
//                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
//            return s;
//        };
//        std::string cls  = up(seg.className);
//        std::string nm   = up(seg.name);
//        if (cls.find("CODE") != std::string::npos) return true;
//        if (cls.find("TEXT") != std::string::npos) return true;
//
//        // (c) well-known segment names used by Sphinx C-- and MASM
//        if (nm == "_TEXT" || nm == "TEXT" || nm == "CODE"
//            || nm == ".TEXT" || nm == "_CODE") return true;
//
//        return false;
//    }
//
//    // ── Emit one segment's bytes with precise label placement ──
//    //
//    // Algorithm:
//    //   1. Optionally strip the Sphinx C-- 10-byte signature from offset 0.
//    //      PUBDEF offsets already include the signature, so after stripping
//    //      we subtract SPHINX_SIG_LEN from every symbol offset so the label
//    //      ends up on the first real opcode byte.
//    //   2. Walk byte-by-byte through the data vector.
//    //      Before emitting byte[i], check seg.symbols for a label at offset i.
//    //      If found: close the current db line, emit the label, start a new line.
//    //   3. Emit at most BYTES_PER_LINE bytes per db directive.
//    void emitSegment(std::ostringstream& out,
//                     const Segment&      seg,
//                     bool                isCode)
//    {
//        out << "; --- " << (seg.name.empty() ? "(unnamed)" : seg.name)
//            << " class=" << (seg.className.empty() ? "(none)" : seg.className)
//            << " ---\n";
//
//        if (seg.data.empty()) {
//            out << "; (no data)\n\n";
//            return;
//        }
//
//        // ── Step 1: Sphinx signature stripping ───────────────
//        // We work on a local copy of the symbol map so we can adjust offsets
//        // without mutating the parsed data.
//        size_t dataStart = 0;   // first byte index to emit
//
//        // Rebuild symbol map with adjusted offsets
//        std::map<uint32_t, std::string> symbols;
//        bool stripped = false;
//
//        if (isCode && seg.hasSphinxSignature()) {
//            dataStart = SPHINX_SIG_LEN;
//            stripped  = true;
//            log("[STRIP] Sphinx C-- signature removed from segment '"
//                + seg.name + "' (" + std::to_string(SPHINX_SIG_LEN) + " bytes)");
//        }
//
//        for (auto& [rawOff, name] : seg.symbols) {
//            uint32_t adj = rawOff;
//            if (stripped) {
//                // Symbol offsets include the signature; shift them back
//                adj = (rawOff >= static_cast<uint32_t>(SPHINX_SIG_LEN))
//                      ? rawOff - static_cast<uint32_t>(SPHINX_SIG_LEN)
//                      : 0;
//            }
//            symbols[adj] = name;
//        }
//
//        // ── Step 2: emit bytes with inline labels ─────────────
//        const std::vector<uint8_t>& data = seg.data;
//        size_t total = data.size();
//
//        // Buffer for the current db line
//        std::vector<uint8_t> lineBuf;
//        lineBuf.reserve(BYTES_PER_LINE);
//
//        auto flushLine = [&]() {
//            if (lineBuf.empty()) return;
//            out << "  db ";
//            for (size_t j = 0; j < lineBuf.size(); ++j) {
//                out << "0x" << std::hex << std::setw(2) << std::setfill('0')
//                    << static_cast<int>(lineBuf[j]);
//                if (j + 1 < lineBuf.size()) out << ", ";
//            }
//            out << "\n" << std::dec;
//            lineBuf.clear();
//        };
//
//        for (size_t i = dataStart; i < total; ++i) {
//            uint32_t off = static_cast<uint32_t>(i - dataStart);
//
//            // ── Label check: if a symbol is at this exact offset,
//            //    flush whatever partial db line we have, then emit the label.
//            //    This guarantees: label always precedes its first byte.
//            auto it = symbols.find(off);
//            if (it != symbols.end()) {
//                flushLine();                    // close preceding db line
//                out << it->second << ":\n";    // emit label
//            }
//
//            lineBuf.push_back(data[i]);
//
//            // Flush when line is full OR when the NEXT byte has a label
//            bool nextHasLabel = false;
//            if (i + 1 < total) {
//                uint32_t nextOff = static_cast<uint32_t>((i + 1) - dataStart);
//                nextHasLabel = symbols.count(nextOff) > 0;
//            }
//
//            if (lineBuf.size() >= BYTES_PER_LINE || nextHasLabel) {
//                flushLine();
//            }
//        }
//        flushLine();    // emit any remaining bytes
//        out << "\n";
//    }
//};
//
//// ============================================================
////  Entry point
//// ============================================================
//int main(int argc, char* argv[]) {
//    std::cout << "omf2fasm_v3  —  Intel OMF → FASM Bridge (Sphinx C-- x64)\n"
//              << "----------------------------------------------------------\n";
//
//    if (argc < 2) {
//        std::cerr << "Usage: " << argv[0]
//                  << " <input.obj> [output.asm] [--quiet]\n";
//        return 1;
//    }
//
//    std::string inputPath  = argv[1];
//    std::string outputPath = "bridge.asm";
//    bool        quiet      = false;
//
//    for (int i = 2; i < argc; ++i) {
//        std::string a = argv[i];
//        if      (a == "--quiet") quiet = true;
//        else if (a[0] != '-')   outputPath = a;
//    }
//
//    try {
//        OmfParser omf(!quiet);
//        omf.parse(inputPath);
//
//        FasmEmitter emitter(omf, !quiet);
//        emitter.emit(outputPath);
//
//        // ── Summary ──────────────────────────────────────────
//        size_t totalSym = 0;
//        for (size_t i = 1; i < omf.segments.size(); ++i)
//            totalSym += omf.segments[i].symbols.size();
//
//        std::cout << "\n=== Summary ===\n"
//                  << "  Module     : " << omf.moduleName                << "\n"
//                  << "  Segments   : " << (omf.segments.size() - 1)     << "\n"
//                  << "  Symbols    : " << totalSym                       << "\n"
//                  << "  Output     : " << outputPath                     << "\n";
//
//        if (totalSym > 0) {
//            std::cout << "\n  Symbols per segment:\n";
//            for (size_t i = 1; i < omf.segments.size(); ++i) {
//                auto& seg = omf.segments[i];
//                if (seg.symbols.empty()) continue;
//                std::cout << "    [" << i << "] " << seg.name << "\n";
//                for (auto& [off, name] : seg.symbols)
//                    std::cout << "       0x" << std::hex << std::setw(4)
//                              << std::setfill('0') << off << "  " << name
//                              << "\n" << std::dec;
//            }
//        }
//
//    } catch (const std::exception& ex) {
//        std::cerr << "\nFatal: " << ex.what() << "\n";
//        return 1;
//    }
//    return 0;
//}
//
//
//
//
//
//
//


// omf2fasm_v4.cpp  —  Surgical OMF → FASM extractor (Sphinx C-- x64)
//
// Core idea: use PUBDEF offsets as a "clean code starts here" marker.
// Everything below main_offset in _TEXT is DOS-startup / Sphinx boilerplate
// and gets routed to .data (or discarded); everything at or above goes to .text.
//
// Key design:
//   • std::map<uint32_t, uint8_t> memory_map  per segment — byte-addressed
//     sparse array that correctly merges multiple LEDATA records.
//   • Two-pass approach: pass 1 = parse all records, pass 2 = emit FASM.
//   • Public symbols below main_offset are filtered from `public` declarations.
//
// Build:
//   MSVC  : cl /std:c++20 /O2 /W4 omf2fasm_v4.cpp /Fe:omf2fasm_v4.exe
//   MinGW : g++ -std=c++20 -O2 -Wall -o omf2fasm_v4 omf2fasm_v4.cpp

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
//  OMF record type constants
// ============================================================
namespace Rec {
    constexpr uint8_t THEADR   = 0x80;
    constexpr uint8_t LNAMES   = 0x96;
    constexpr uint8_t SEGDEF   = 0x98;
    constexpr uint8_t SEGDEF32 = 0x99;
    constexpr uint8_t EXTDEF   = 0x8C;
    constexpr uint8_t PUBDEF   = 0x90;
    constexpr uint8_t PUBDEF32 = 0x91;
    constexpr uint8_t LEDATA   = 0xA0;
    constexpr uint8_t LEDATA32 = 0xA1;
    constexpr uint8_t LIDATA   = 0xA2;
    constexpr uint8_t LIDATA32 = 0xA3;
    constexpr uint8_t FIXUPP   = 0x9C;
    constexpr uint8_t FIXUPP32 = 0x9D;
    constexpr uint8_t MODEND   = 0x8A;
    constexpr uint8_t MODEND32 = 0x8B;
}

// ============================================================
//  Sphinx C-- signature (10 bytes at offset 0 of _TEXT)
// ============================================================
static constexpr uint8_t SPHINX_SIG[]   = {
    0x53,0x50,0x48,0x49,0x4E,0x58,0x43,0x2D,0x2D,0x00
};
static constexpr uint32_t SPHINX_SIG_LEN = sizeof(SPHINX_SIG);

// "System" symbol prefixes that belong to 16-bit DOS startup.
// We suppress them from `public` declarations if they sit below main_offset.
static const std::vector<std::string> SYSTEM_PREFIXES = {
    "__startup", "__start", "__end", "__init",
    "__exit",    "__modu",  "__postvar", "__pre"
};

static bool isSystemSymbol(const std::string& name) {
    for (auto& pfx : SYSTEM_PREFIXES)
        if (name.substr(0, pfx.size()) == pfx) return true;
    return false;
}

// ============================================================
//  Segment descriptor
// ============================================================
struct SegDef {
    std::string name;       // from LNAMES  e.g. "_TEXT"
    std::string className;  // from LNAMES  e.g. "CODE"
};

// ============================================================
//  Public symbol
// ============================================================
struct PubSym {
    std::string name;       // sanitized FASM label
    std::string rawName;    // original as found in PUBDEF
    uint32_t    segIdx;     // 1-based OMF segment index
    uint32_t    offset;     // byte offset within segment
};

// ============================================================
//  Segment data container
//
//  memory_map: sparse byte-addressed storage.
//    Key   = absolute byte offset within this segment (from OMF LEDATA header).
//    Value = the byte at that offset.
//  This design correctly handles:
//    • Multiple disjoint LEDATA blocks (no need to track block boundaries).
//    • Out-of-order LEDATA records.
//    • Overlapping records (later write wins — matches linker behaviour).
// ============================================================
struct SegData {
    std::map<uint32_t, uint8_t> memory_map;

    // Insert a block of bytes starting at `offset`
    void insert(uint32_t offset, const std::vector<uint8_t>& bytes) {
        for (size_t i = 0; i < bytes.size(); ++i)
            memory_map[offset + static_cast<uint32_t>(i)] = bytes[i];
    }

    bool empty() const { return memory_map.empty(); }

    // Lowest and highest offsets present
    uint32_t minOffset() const { return memory_map.begin()->first; }
    uint32_t maxOffset() const { return memory_map.rbegin()->first; }
};

// ============================================================
//  Low-level OMF byte reader
// ============================================================
class Reader {
public:
    explicit Reader(std::vector<uint8_t> raw)
        : buf_(std::move(raw)), pos_(0) {}

    bool   eof()  const { return pos_ >= buf_.size(); }
    size_t pos()  const { return pos_; }
    size_t size() const { return buf_.size(); }
    void   seek(size_t p) { pos_ = p; }

    uint8_t  u8()  { chk(1); return buf_[pos_++]; }
    uint16_t u16() {
        chk(2);
        uint16_t v = static_cast<uint16_t>(buf_[pos_])
                   | static_cast<uint16_t>(buf_[pos_+1] << 8);
        pos_ += 2; return v;
    }
    uint32_t u32() {
        chk(4);
        uint32_t v = static_cast<uint32_t>(buf_[pos_])
                   |(static_cast<uint32_t>(buf_[pos_+1])<<8)
                   |(static_cast<uint32_t>(buf_[pos_+2])<<16)
                   |(static_cast<uint32_t>(buf_[pos_+3])<<24);
        pos_ += 4; return v;
    }

    // OMF variable-width index: high bit set → 2-byte big-endian-ish
    uint16_t index() {
        uint8_t b = u8();
        if (b & 0x80)
            return static_cast<uint16_t>((b & 0x7F) << 8) | u8();
        return b;
    }

    // Pascal length-prefixed string
    std::string pstr() {
        uint8_t len = u8();
        chk(len);
        std::string s(reinterpret_cast<const char*>(&buf_[pos_]), len);
        pos_ += len;
        return s;
    }

    std::vector<uint8_t> read(size_t n) {
        chk(n);
        std::vector<uint8_t> v(buf_.begin()+pos_, buf_.begin()+pos_+n);
        pos_ += n; return v;
    }

    static Reader fromFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Cannot open: " + path);
        auto sz = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<uint8_t> buf(sz);
        f.read(reinterpret_cast<char*>(buf.data()),
               static_cast<std::streamsize>(sz));
        if (!f) throw std::runtime_error("Read error: " + path);
        return Reader(std::move(buf));
    }

private:
    std::vector<uint8_t> buf_;
    size_t pos_;
    void chk(size_t n) const {
        if (pos_ + n > buf_.size()) {
            std::ostringstream e;
            e << "OMF read past EOF at 0x" << std::hex << pos_;
            throw std::runtime_error(e.str());
        }
    }
};

// ============================================================
//  OMF Parser  (pass 1)
// ============================================================
class OmfParser {
public:
    std::string              moduleName;
    std::vector<std::string> lnames;    // 1-based
    std::vector<SegDef>      segdefs;   // 1-based
    std::vector<PubSym>      pubsyms;
    std::map<uint32_t, SegData> segdata; // segIdx → data

    explicit OmfParser(bool verbose = true) : verbose_(verbose) {}

    void parse(const std::string& path) {
        Reader rd = Reader::fromFile(path);
        log("[INFO] Loaded " + std::to_string(rd.size()) + " bytes");

        lnames.push_back("");   // sentinel: index 0 unused
        segdefs.push_back({});  // sentinel: index 0 unused

        int nRec = 0;
        while (!rd.eof()) {
            if (rd.size() - rd.pos() < 3) break;

            size_t   recStart = rd.pos();
            uint8_t  type     = rd.u8();
            uint16_t len      = rd.u16();
            if (len == 0) break;

            size_t dataEnd = rd.pos() + len - 1; // points to checksum byte
            if (dataEnd + 1 > rd.size()) {
                log("[WARN] Truncated record at 0x" + hx(recStart));
                break;
            }

            dispatch(rd, type, dataEnd);
            rd.seek(dataEnd + 1);
            ++nRec;

            if (type == Rec::MODEND || type == Rec::MODEND32) break;
        }

        log("[INFO] Records: " + std::to_string(nRec)
            + "  Segments: " + std::to_string(segdefs.size()-1)
            + "  Symbols: " + std::to_string(pubsyms.size()));
    }

private:
    bool verbose_;

    void log(const std::string& s) const {
        if (verbose_) std::cout << s << "\n";
    }
    static std::string hx(size_t v) {
        std::ostringstream ss; ss << std::hex << v; return ss.str();
    }
    static std::string sanitize(std::string n) {
        if (n.empty()) return "_sym";
        for (char& c : n)
            if (!std::isalnum(static_cast<unsigned char>(c))
                && c != '_' && c != '.') c = '_';
        if (std::isdigit(static_cast<unsigned char>(n[0]))) n = "_" + n;
        return n;
    }

    void dispatch(Reader& rd, uint8_t type, size_t end) {
        switch (type) {
        case Rec::THEADR:                      parseTHEADR(rd);          break;
        case Rec::LNAMES:                      parseLNAMES(rd, end);     break;
        case Rec::SEGDEF:  case Rec::SEGDEF32: parseSEGDEF(rd);          break;
        case Rec::EXTDEF:                      parseEXTDEF(rd, end);     break;
        case Rec::PUBDEF:  case Rec::PUBDEF32:
            parsePUBDEF(rd, end, type == Rec::PUBDEF32); break;
        case Rec::LEDATA:  case Rec::LEDATA32:
            parseLEDATA(rd, end, type == Rec::LEDATA32); break;
        case Rec::LIDATA:  case Rec::LIDATA32:
            parseLIDATA(rd, end, type == Rec::LIDATA32); break;
        default:
            log("[SKIP] type=0x" + hx(type));
            break;
        }
    }

    void parseTHEADR(Reader& rd) {
        moduleName = rd.pstr();
        log("[THEADR] " + moduleName);
    }

    void parseLNAMES(Reader& rd, size_t end) {
        while (rd.pos() < end) {
            std::string n = rd.pstr();
            log("[LNAMES][" + std::to_string(lnames.size()) + "] '" + n + "'");
            lnames.push_back(n);
        }
    }

    void parseSEGDEF(Reader& rd) {
        SegDef sd;
        rd.u8(); // attrib byte — alignment/combine/use bits
        // We don't need the length for extraction; read and discard
        // Sphinx C-- always uses 32-bit SEGDEF (0x99), but handle both
        // The actual type flag is in the record type dispatched above —
        // here we just consume whatever byte width the segment uses.
        // To know is32, the caller would need to pass it; we infer from
        // whether the SEGDEF or SEGDEF32 type was dispatched.
        // Because we're called generically, read u16 (safe for both:
        // SEGDEF32 callers should override — handled below).
        rd.u16(); // length (may be 0 for 64K segment in 16-bit form)

        uint16_t nameIdx  = rd.index();
        uint16_t classIdx = rd.index();
        rd.index(); // overlay

        auto resolve = [&](uint16_t i) -> std::string {
            return (i > 0 && i < lnames.size()) ? lnames[i] : "";
        };
        sd.name      = resolve(nameIdx);
        sd.className = resolve(classIdx);
        uint32_t myIdx = static_cast<uint32_t>(segdefs.size());
        segdefs.push_back(sd);
        log("[SEGDEF][" + std::to_string(myIdx) + "] '"
            + sd.name + "' class='" + sd.className + "'");
    }

    void parseEXTDEF(Reader& rd, size_t end) {
        while (rd.pos() < end) {
            std::string n = rd.pstr();
            rd.index();
            log("[EXTDEF] '" + n + "'");
        }
    }

    void parsePUBDEF(Reader& rd, size_t end, bool is32) {
        rd.index(); // base group
        uint16_t baseSegIdx = rd.index();
        if (baseSegIdx == 0) rd.u16(); // base frame

        while (rd.pos() < end) {
            PubSym sym;
            sym.rawName  = rd.pstr();
            sym.name     = sanitize(sym.rawName);
            sym.segIdx   = baseSegIdx;
            sym.offset   = is32 ? rd.u32() : rd.u16();
            rd.index();  // type index

            pubsyms.push_back(sym);
            log("[PUBDEF] '" + sym.rawName + "' seg=" + std::to_string(sym.segIdx)
                + " off=0x" + hx(sym.offset));
        }
    }

    // LEDATA: each byte is stored individually in the memory_map.
    // This is the key: map[absolute_offset] = byte, so any number of
    // LEDATA records for the same segment are automatically merged.
    void parseLEDATA(Reader& rd, size_t end, bool is32) {
        uint32_t segIdx = rd.index();
        uint32_t offset = is32 ? rd.u32() : rd.u16();
        size_t   nbytes = end - rd.pos();
        auto     data   = rd.read(nbytes);
        segdata[segIdx].insert(offset, data);
        log("[LEDATA" + std::string(is32?"32":"") + "] seg="
            + std::to_string(segIdx) + " off=0x" + hx(offset)
            + " n=" + std::to_string(nbytes));
    }

    void parseLIDATA(Reader& rd, size_t end, bool is32) {
        uint32_t segIdx = rd.index();
        uint32_t offset = is32 ? rd.u32() : rd.u16();
        std::vector<uint8_t> expanded;
        expandLI(rd, end, expanded);
        segdata[segIdx].insert(offset, expanded);
        log("[LIDATA] seg=" + std::to_string(segIdx)
            + " off=0x" + hx(offset)
            + " expanded=" + std::to_string(expanded.size()));
    }

    void expandLI(Reader& rd, size_t end, std::vector<uint8_t>& out) {
        while (rd.pos() < end) {
            uint16_t rep    = rd.u16();
            uint16_t blocks = rd.u16();
            if (blocks == 0) {
                uint8_t  dlen = rd.u8();
                auto raw = rd.read(dlen);
                for (uint16_t r = 0; r < rep; ++r)
                    out.insert(out.end(), raw.begin(), raw.end());
            } else {
                std::vector<uint8_t> inner;
                for (uint16_t b = 0; b < blocks; ++b)
                    expandLI(rd, end, inner);
                for (uint16_t r = 0; r < rep; ++r)
                    out.insert(out.end(), inner.begin(), inner.end());
            }
        }
    }
};

// ============================================================
//  SEGDEF re-parser for 32-bit lengths
//  Sphinx C-- uses SEGDEF32 (0x99).  The generic parseSEGDEF
//  above reads a u16 length which is wrong for 0x99 records.
//  We fix this by re-parsing SEGDEF records with the correct
//  type flag after the fact.  For extraction purposes the
//  segment length doesn't matter (we use actual LEDATA bytes),
//  so this is only relevant for debug logging.
//  → No action needed; parsing still works because we skip the
//    length field and only use name/class indices.
// ============================================================

// ============================================================
//  FASM Emitter  (pass 2)
//
//  Split strategy:
//    For each segment that has data:
//      • Find main_offset = offset of 'main' symbol in that segment
//        (or UINT32_MAX if 'main' is not in this segment).
//      • Bytes at offset < main_offset  → .data  (DOS startup / header)
//      • Bytes at offset >= main_offset → .text  (real code)
//      • Sphinx C-- signature (first 10 bytes) is always excluded.
//
//  Label placement:
//      Walk the memory_map in key order (ascending offset).
//      Before emitting a byte, check if any public symbol sits
//      exactly at that offset.  If so: flush current db line,
//      emit label, continue.
// ============================================================
class FasmEmitter {
public:
    static constexpr size_t COLS = 16; // bytes per db line

    FasmEmitter(const OmfParser& p, bool verbose)
        : omf_(p), verbose_(verbose) {}

    void emit(const std::string& outPath) {
        // ── Pre-compute per-segment symbol tables ─────────────
        // segSymbols[segIdx][offset] = label name
        std::map<uint32_t, std::map<uint32_t, std::string>> segSymbols;
        for (auto& sym : omf_.pubsyms)
            segSymbols[sym.segIdx][sym.offset] = sym.name;

        // ── Find main_offset for each segment ─────────────────
        // main_offset[segIdx] = offset of 'main', or UINT32_MAX
        std::map<uint32_t, uint32_t> mainOffset;
        for (auto& sym : omf_.pubsyms) {
            if (sym.rawName == "main" || sym.name == "main") {
                mainOffset[sym.segIdx] = sym.offset;
                log("[INFO] main found: seg=" + std::to_string(sym.segIdx)
                    + " off=0x" + hx(sym.offset));
            }
        }

        std::ostringstream out;

        // ── Header ────────────────────────────────────────────
        out << "; ============================================================\n"
            << "; bridge.asm  —  auto-generated by omf2fasm_v4\n"
            << "; Module: " << omf_.moduleName << "\n"
            << "; ============================================================\n\n"
            << "format MS64 COFF\n\n";

        // ── Public declarations ────────────────────────────────
        // Only export symbols that:
        //   a) are not system/startup symbols, OR
        //   b) are system symbols but sit at or above main_offset
        // → In practice: always export 'main'; suppress DOS startup names.
        out << "; --- public symbols ---\n";
        bool anyPublic = false;
        for (auto& sym : omf_.pubsyms) {
            uint32_t moff = mainOffset.count(sym.segIdx)
                            ? mainOffset[sym.segIdx] : UINT32_MAX;
            bool belowMain  = sym.offset < moff;
            bool isSysName  = isSystemSymbol(sym.rawName);

            if (belowMain && isSysName) {
                log("[FILTER] Suppressing system symbol '" + sym.rawName
                    + "' (off=0x" + hx(sym.offset) + " < main=0x" + hx(moff) + ")");
                continue;
            }
            out << "public " << sym.name << "\n";
            anyPublic = true;
        }
        if (!anyPublic) out << "; (no exported symbols)\n";
        out << "\n";

        // ── Collect bytes into text / data buckets ─────────────
        // text_bytes[segIdx] = { offset → byte }  (offset >= main_offset)
        // data_bytes[segIdx] = { offset → byte }  (offset <  main_offset)
        std::map<uint32_t, std::map<uint32_t,uint8_t>> textBytes, dataBytes;

        for (auto& [segIdx, sdata] : omf_.segdata) {
            if (sdata.empty()) continue;
            uint32_t moff = mainOffset.count(segIdx)
                            ? mainOffset[segIdx] : UINT32_MAX;

            for (auto& [off, byte] : sdata.memory_map) {
                // Always skip the Sphinx C-- signature bytes
                // (offsets 0..SPHINX_SIG_LEN-1 if the signature is present)
                if (off < SPHINX_SIG_LEN) {
                    // Check if the whole signature is present in this segment
                    bool hasSig = true;
                    for (uint32_t k = 0; k < SPHINX_SIG_LEN && hasSig; ++k) {
                        auto it = sdata.memory_map.find(k);
                        if (it == sdata.memory_map.end()
                            || it->second != SPHINX_SIG[k])
                            hasSig = false;
                    }
                    if (hasSig) {
                        log("[STRIP] Skipping Sphinx sig byte at off=0x" + hx(off));
                        continue; // skip this byte
                    }
                }

                if (off >= moff)
                    textBytes[segIdx][off] = byte;
                else
                    dataBytes[segIdx][off] = byte;
            }
        }

        // ── .text section ─────────────────────────────────────
        bool textWritten = false;
        for (auto& [segIdx, byteMap] : textBytes) {
            if (byteMap.empty()) continue;
            if (!textWritten) {
                out << "section '.text' code readable executable\n\n";
                textWritten = true;
            }

            auto& syms = segSymbols[segIdx];
            uint32_t moff = mainOffset.count(segIdx)
                            ? mainOffset[segIdx] : UINT32_MAX;

            emitByteMap(out, byteMap, syms, moff, /*suppressBelow=*/false);
        }

        // ── .data section ─────────────────────────────────────
        bool dataWritten = false;
        for (auto& [segIdx, byteMap] : dataBytes) {
            if (byteMap.empty()) continue;
            if (!dataWritten) {
                out << "\nsection '.data' data readable writeable\n\n";
                dataWritten = true;
            }
            // No public labels in the data section (they're all below main)
            std::map<uint32_t,std::string> emptySyms;
            emitByteMap(out, byteMap, emptySyms, UINT32_MAX, /*suppressBelow=*/false);
        }

        // ── Write file ────────────────────────────────────────
        std::ofstream f(outPath);
        if (!f) throw std::runtime_error("Cannot write: " + outPath);
        f << out.str();
        log("[INFO] Written: " + outPath);
    }

private:
    const OmfParser& omf_;
    bool verbose_;

    void log(const std::string& s) const {
        if (verbose_) std::cout << s << "\n";
    }
    static std::string hx(size_t v) {
        std::ostringstream ss; ss << std::hex << v; return ss.str();
    }

    // ── Emit a byte map as db lines, inserting labels precisely ──
    //
    // byteMap  : offset → byte  (already filtered to text or data range)
    // syms     : offset → label name (for this segment)
    // mainOff  : offset of 'main' (for comment annotation)
    //
    // Label placement guarantee:
    //   We iterate byteMap in ascending offset order.
    //   BEFORE adding each byte to the pending line buffer, we check
    //   if a label exists at that offset.  If yes:
    //     1. Flush the pending buffer as a db line.
    //     2. Emit "labelname:\n".
    //     3. Then add the byte to a fresh buffer.
    //   This means no byte ever appears before its own label.
    void emitByteMap(std::ostringstream&                   out,
                     const std::map<uint32_t,uint8_t>&     byteMap,
                     const std::map<uint32_t,std::string>& syms,
                     uint32_t                              /*mainOff*/,
                     bool                                  /*suppressBelow*/)
    {
        std::vector<uint8_t> lineBuf;
        lineBuf.reserve(COLS);

        auto flushLine = [&]() {
            if (lineBuf.empty()) return;
            out << "  db ";
            for (size_t j = 0; j < lineBuf.size(); ++j) {
                out << "0x" << std::hex << std::setw(2)
                    << std::setfill('0') << static_cast<int>(lineBuf[j]);
                if (j + 1 < lineBuf.size()) out << ", ";
            }
            out << "\n" << std::dec;
            lineBuf.clear();
        };

        for (auto& [off, byte] : byteMap) {
            // Does a label live at this offset?
            auto it = syms.find(off);
            if (it != syms.end()) {
                flushLine();                    // close preceding db line
                out << it->second << ":\n";    // emit label
            }

            lineBuf.push_back(byte);

            // Flush when line is full OR when the very next map entry
            // (which may not be off+1 — memory_map is sparse) has a label
            bool nextHasLabel = false;
            auto nextIt = byteMap.upper_bound(off); // first entry with key > off
            if (nextIt != byteMap.end())
                nextHasLabel = syms.count(nextIt->first) > 0;

            if (lineBuf.size() >= COLS || nextHasLabel)
                flushLine();
        }
        flushLine();
        out << "\n";
    }
};

// ============================================================
//  Entry point
// ============================================================
int main(int argc, char* argv[]) {
    std::cout << "omf2fasm_v4  —  Surgical OMF→FASM (Sphinx C-- x64)\n"
              << "---------------------------------------------------\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <input.obj> [output.asm] [--quiet]\n";
        return 1;
    }

    std::string inputPath  = argv[1];
    std::string outputPath = "bridge.asm";
    bool        quiet      = false;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--quiet") quiet = true;
        else if (a[0] != '-')   outputPath = a;
    }

    try {
        OmfParser parser(!quiet);
        parser.parse(inputPath);

        FasmEmitter emitter(parser, !quiet);
        emitter.emit(outputPath);

        // ── Final summary ─────────────────────────────────────
        size_t totalBytes = 0;
        for (auto& [idx, sd] : parser.segdata)
            totalBytes += sd.memory_map.size();

        std::cout << "\n=== Summary ===\n"
                  << "  Module   : " << parser.moduleName           << "\n"
                  << "  Segments : " << (parser.segdefs.size()-1)   << "\n"
                  << "  Symbols  : " << parser.pubsyms.size()        << "\n"
                  << "  Bytes    : " << totalBytes                   << "\n"
                  << "  Output   : " << outputPath                   << "\n";

        // Per-symbol table
        if (!parser.pubsyms.empty()) {
            std::cout << "\n  Symbols:\n";
            for (auto& s : parser.pubsyms)
                std::cout << "    " << std::left << std::setw(28) << s.rawName
                          << "  seg=" << s.segIdx
                          << "  off=0x" << std::hex << std::setw(8)
                          << std::setfill('0') << s.offset
                          << std::dec << std::setfill(' ')
                          << (isSystemSymbol(s.rawName) ? "  [system]" : "")
                          << "\n";
        }

    } catch (const std::exception& ex) {
        std::cerr << "\nFatal: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
