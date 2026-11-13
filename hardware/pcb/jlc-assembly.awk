# Builds a JLC CPL (stdout) and BOM (-v bom=FILE) from the same placements, so every BOM designator has exactly one CPL row.
# Usage: awk -F, -v bom=out-bom.csv -f jlc-assembly.awk parts.txt pos.csv > out-cpl.csv
#   parts.txt: kicad-cli sch export bom, one reference per row, "Reference|Value|Footprint|LCSC"
#   pos.csv:   kicad-cli pcb export pos --format csv
# Panel references carry KiKit's _N board suffix. Only schematic BOM parts are placed, so test points, jumpers and the logo stay out.

NR == FNR {
    if (FNR > 1) {
        split($0, field, "|")
        partGroup[field[1]] = field[2] "|" field[3] "|" field[4]
    }
    next
}

FNR == 1 {
    print "Designator,Mid X,Mid Y,Layer,Rotation"
    next
}

{
    gsub(/"/, "")
    ref = $1
    sourceRef = ref
    sub(/_[0-9]+$/, "", sourceRef)
    if (!(sourceRef in partGroup)) next

    printf "%s,%.4fmm,%.4fmm,%s,%d\n", ref, $4, $5, ($7 == "top" ? "Top" : "Bottom"), $6
    group = partGroup[sourceRef]
    groupRefs[group] = (group in groupRefs ? groupRefs[group] "," : "") ref
}

END {
    print "Comment,Designator,Footprint,LCSC Part #" > bom
    for (group in groupRefs) {
        split(group, field, "|")
        printf "\"%s\",\"%s\",\"%s\",\"%s\"\n", field[1], groupRefs[group], field[2], field[3] > bom
    }
}
