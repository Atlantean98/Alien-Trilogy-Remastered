//@category AlienTrilogy
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.file.*;

public class AuditTrilogy extends GhidraScript {
    public void run() throws Exception {
        Path out=Paths.get(getScriptArgs()[0]);
        Files.createDirectories(out);
        AddressSet code=new AddressSet(toAddr(0x10000),toAddr(0x7efce));
        AddressSet decoded=new AddressSet();
        InstructionIterator instructions=currentProgram.getListing().getInstructions(code,true);
        long count=0;
        while(instructions.hasNext()) {
            Instruction i=instructions.next();
            decoded.add(i.getMinAddress(),i.getMaxAddress()); count++;
        }
        AddressSet gaps=code.subtract(decoded);
        try(BufferedWriter writer=Files.newBufferedWriter(out.resolve("undecoded_code_ranges.tsv"))) {
            writer.write("start\tend\tbytes\n");
            for(AddressRange range:gaps) writer.write(range.getMinAddress()+"\t"+range.getMaxAddress()+"\t"+range.getLength()+"\n");
        }
        int inside=0,outside=0;
        try(BufferedWriter calls=Files.newBufferedWriter(out.resolve("calls.tsv"));
            BufferedWriter anomalies=Files.newBufferedWriter(out.resolve("outside_code_functions.tsv"))) {
            calls.write("caller\tcallee\n"); anomalies.write("address\tname\n");
            FunctionIterator functions=currentProgram.getFunctionManager().getFunctions(true);
            while(functions.hasNext()) {
                Function f=functions.next();
                if(code.contains(f.getEntryPoint())) inside++;
                else {outside++; anomalies.write(f.getEntryPoint()+"\t"+f.getName()+"\n");}
                for(Function callee:f.getCalledFunctions(monitor)) calls.write(f.getEntryPoint()+"\t"+callee.getEntryPoint()+"\n");
            }
        }
        try(BufferedWriter strings=Files.newBufferedWriter(out.resolve("strings.tsv"))) {
            strings.write("address\ttext\n");
            DataIterator data=currentProgram.getListing().getDefinedData(true);
            while(data.hasNext()) {
                Data d=data.next(); Object value=d.getValue();
                if(value instanceof String) strings.write(d.getAddress()+"\t"+value.toString().replace("\n","\\n").replace("\r","\\r").replace("\t","\\t")+"\n");
            }
        }
        Files.writeString(out.resolve("coverage.txt"),
            "Functions starting in LE code object: "+inside+"\n"+
            "Functions outside LE code object requiring review: "+outside+"\n"+
            "Instructions in LE code object: "+count+"\n"+
            "Code-object bytes decoded as instructions: "+decoded.getNumAddresses()+"\n"+
            "Remaining code-object bytes: "+gaps.getNumAddresses()+"\n"+
            "Remaining ranges contain a mixture of embedded data, padding and possibly unidentified code.\n"+
            "Instruction coverage is not verified source-code completeness. Calls omit unresolved indirect targets.\n");
        println("Audit complete: "+inside+" code functions, "+outside+" out-of-section candidates.");
    }
}
