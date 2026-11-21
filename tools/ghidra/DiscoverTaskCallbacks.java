//@category AlienTrilogy
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.io.*;

public class DiscoverTaskCallbacks extends GhidraScript {
    public void run() throws Exception {
        Path reports=Paths.get(getScriptArgs()[0]);
        try(BufferedWriter log=Files.newBufferedWriter(reports.resolve("task_callback_discovery.tsv"))) {
            log.write("address\tstatus\n");
            for(String line:Files.readAllLines(reports.resolve("task_callback_seeds.txt"))) {
                long address=Long.parseLong(line.trim(),16);
                if(address<0x10000 || address>=0x7efcf) throw new IllegalArgumentException("Outside code");
                Function at=getFunctionAt(toAddr(address));
                if(at!=null) {log.write(line+"\texisting\n");continue;}
                Function containing=getFunctionContaining(toAddr(address));
                if(containing!=null) {log.write(line+"\tinside_existing_function_"+containing.getEntryPoint()+"\n");continue;}
                disassemble(toAddr(address));
                Function f=createFunction(toAddr(address),"TaskCallback_"+line);
                if(f==null) {log.write(line+"\tcreation_failed\n");continue;}
                setPlateComment(toAddr(address),"Direct constant callback target passed in EDX to CreateTask. See reports/task_registrations.tsv. Gameplay semantics and full signature remain unverified.");
                addEntryPoint(toAddr(address));
                log.write(line+"\tcreated\n");
            }
        }
    }
}
