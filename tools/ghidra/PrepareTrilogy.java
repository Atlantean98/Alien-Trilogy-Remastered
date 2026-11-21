//@category AlienTrilogy
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;

public class PrepareTrilogy extends GhidraScript {
    public void run() throws Exception {
        Path image = Paths.get(getScriptArgs()[0]);
        MemoryBlock code = currentProgram.getMemory().getBlock(toAddr(0x10000));
        code.setName("LE_CODE"); code.setRead(true); code.setWrite(false); code.setExecute(true);
        long[] bases = {0x80000, 0x90000};
        for (int i=0; i<2; i++) {
            Path path = image.resolve("object" + (i+2) + ".bin");
            try (InputStream input = Files.newInputStream(path)) {
                MemoryBlock block = currentProgram.getMemory().createInitializedBlock(
                    "LE_DATA" + (i+2), toAddr(bases[i]), input, Files.size(path), monitor, false);
                block.setRead(true); block.setWrite(true); block.setExecute(false);
            }
        }
        createLabel(toAddr(0x5fed8), "le_entry", true, SourceType.IMPORTED);
        addEntryPoint(toAddr(0x5fed8));
        disassemble(toAddr(0x5fed8)); createFunction(toAddr(0x5fed8), "le_entry");
        // ALTEngine references do not consistently align with this executable.
        // Preserve them in the image inventory, never force them into the listing.
        println("Prepared LE memory map; function discovery starts from the LE entry point.");
    }
}
