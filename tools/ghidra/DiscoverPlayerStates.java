//@category AlienTrilogy
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.*;
import java.nio.file.*;

public class DiscoverPlayerStates extends GhidraScript {
    public void run() throws Exception {
        Path reports=Paths.get(getScriptArgs()[0]);
        // Evidence: initializer writes 0x3f908 to task+4; 0x3f908 writes the
        // other two values to task+4 when its signed height comparison changes.
        long[] addresses={0x3f908,0x3e3c4,0x3e638};
        try(BufferedWriter writer=Files.newBufferedWriter(reports.resolve("player_state_discovery.tsv"))) {
            writer.write("address\tstatus\n");
            for(long value:addresses) {
                String address=String.format("%08x",value);
                Function f=getFunctionAt(toAddr(value));
                if(f!=null) {writer.write(address+"\texisting\n");continue;}
                Function enclosing=getFunctionContaining(toAddr(value));
                if(enclosing!=null) {writer.write(address+"\tinside_existing_"+enclosing.getEntryPoint()+"\n");continue;}
                disassemble(toAddr(value));
                f=createFunction(toAddr(value),"PlayerState_"+address);
                if(f==null) {writer.write(address+"\tcreation_failed\n");continue;}
                addEntryPoint(toAddr(value));
                setPlateComment(toAddr(value),"Player task callback transition target verified from task+4 stores. See reports/player-movement.md. Coordinate and state semantics still under investigation.");
                writer.write(address+"\tcreated\n");
            }
        }
    }
}
