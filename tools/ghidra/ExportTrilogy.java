//@category AlienTrilogy
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;

public class ExportTrilogy extends GhidraScript {
    public void run() throws Exception {
        Path out = Paths.get(getScriptArgs()[0]);
        Files.createDirectories(out.resolve("functions"));
        // Remove only this exporter's per-address output from earlier analysis passes.
        try (java.util.stream.Stream<Path> files=Files.list(out.resolve("functions"))) {
            for (Path file : files.filter(p -> p.getFileName().toString().matches("[0-9a-fA-F]{8}\\.c")).toList()) Files.delete(file);
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        int total=0, ok=0;
        try (BufferedWriter index=Files.newBufferedWriter(out.resolve("functions.tsv"));
             BufferedWriter combined=Files.newBufferedWriter(out.resolve("TRILOGY.raw.c"))) {
            index.write("address\tname\tbody_bytes\tstatus\terror\n");
            combined.write("/* MACHINE-GENERATED RESEARCH PSEUDOCODE. Not buildable C/C++.\n * Types, calling conventions, function boundaries and semantics require verification. */\n\n");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while(functions.hasNext() && !monitor.isCancelled()) {
                Function function=functions.next(); total++;
                DecompileResults result=decompiler.decompileFunction(function, 45, monitor);
                String error=result.getErrorMessage();
                boolean success=result.decompileCompleted() && result.getDecompiledFunction()!=null;
                index.write(function.getEntryPoint()+"\t"+function.getName()+"\t"+function.getBody().getNumAddresses()+"\t"+(success?"decompiled":"failed")+"\t"+(error==null?"":error.replace('\n',' ').replace('\t',' '))+"\n");
                if(success) {
                    ok++;
                    String text="/* Address: "+function.getEntryPoint()+" */\n"+result.getDecompiledFunction().getC()+"\n";
                    Files.writeString(out.resolve("functions").resolve(function.getEntryPoint()+".c"),text,StandardCharsets.UTF_8);
                    combined.write(text);
                }
                if(total%100==0) println("Exported "+total+" functions; "+ok+" decompiled");
            }
        } finally { decompiler.dispose(); }
        try (BufferedWriter asm=Files.newBufferedWriter(out.resolve("TRILOGY.asm"))) {
            InstructionIterator instructions=currentProgram.getListing().getInstructions(true);
            while(instructions.hasNext()) {
                Instruction instruction=instructions.next();
                asm.write(instruction.getAddress()+"  "+instruction.toString()+"\n");
            }
        }
        long instructions=currentProgram.getListing().getNumInstructions();
        Files.writeString(out.resolve("summary.txt"),"Identified functions: "+total+"\nDecompiled functions: "+ok+"\nFailed functions: "+(total-ok)+"\nDecoded instructions: "+instructions+"\nNot a completeness claim. Unidentified code, indirect calls, embedded data and incorrect prototypes require review.\n");
        println("Export complete: "+ok+"/"+total);
    }
}
