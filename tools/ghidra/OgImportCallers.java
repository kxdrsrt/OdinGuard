// Lists functions calling the given imported APIs (script args), with call counts.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.Function;
import java.util.*;

public class OgImportCallers extends GhidraScript {
    public void run() throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        for (String name : getScriptArgs()) {
            Map<String,Integer> funcs = new TreeMap<>();
            List<Symbol> syms = new ArrayList<>();
            for (Symbol s : st.getExternalSymbols(name)) syms.add(s);
            for (Symbol s : st.getSymbols(name)) syms.add(s);
            for (Symbol s : syms) {
                for (Reference r : getReferencesTo(s.getAddress())) {
                    List<Reference> refs = new ArrayList<>();
                    for (Reference r2 : getReferencesTo(r.getFromAddress())) refs.add(r2);
                    if (refs.isEmpty()) refs.add(r);
                    for (Reference r2 : refs) {
                        Function f = getFunctionContaining(r2.getFromAddress());
                        String k = f == null ? "?@" + r2.getFromAddress() : f.getName() + "@" + f.getEntryPoint();
                        funcs.merge(k, 1, Integer::sum);
                    }
                }
            }
            println("API " + name + " callers=" + funcs.size() + " " + funcs);
        }
    }
}
