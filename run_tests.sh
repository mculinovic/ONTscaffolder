#!/usr/bin/env bash

# @file run_tests.sh
# @author Marko Culinovic <marko.culinoivc@gmail.com>
# @brief Script used for testing scaffolder methods

# usage
if [[ $# -ne 3 ]]; then
    echo "usage: $0 <ont_reads.fasta> <draft_genome.fasta> <reference_genome.fasta>"
    exit 1
fi

name="eagler"
tmp_dir="./tmp"

if [[ ! -e $tmp_dir ]]; then
   mkdir $tmp_dir
elif [[ ! -d $tmp_dir ]]; then
   echo "$tmp_dir already exists but is not a tmp_directory" 1>&2
fi

# Make time only output elapsed time in seconds
TIMEFORMAT=%R


#indexing draft genome
echo "[BWA] indexing $2"
echo "..."
bwa_index="bwa index $2"
# Keep stdout unmolested
exec 3>&1
{ time $bwa_index 1>&3; } 2>&1 | awk '{
    printf "[BWA] indexing finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
exec 3>&-
echo ""

#acquiring number of processor cores for multithreading
num_threads=$(grep -c ^processor /proc/cpuinfo)

#aligning long reads to draft genome
echo "[BWA] aligning reads to draft genome"
echo "..."
bwa_mem="bwa mem -t $num_threads -x pacbio -Y $2 $1"
{ time $bwa_mem > "./tmp/aln.sam"; } 2>&1 | awk '{
    printf "[BWA] alignment finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


# filenames for scaffolder output
poa_file="./tmp/poa.fasta"
gr_file="./tmp/gr.fasta"
gm_file="./tmp/gm.fasta"
poa_ext_file="./tmp/poa_ext.fasta"
gr_ext_file="./tmp/gr_ext.fasta"
gm_ext_file="./tmp/gm_ext.fasta"
poa_aln_file="./tmp/poa.sam"
gr_aln_file="./tmp/gr.sam"
gm_aln_file="./tmp/gm.sam"


# running scaffolder with global realign
echo "[$name] extending contigs using global realignment method"
echo "..."
{ time ./release/$name $1 $2 $gr_file $gr_ext_file; } 2>&1 | awk '{
    printf "[scaffolder] extension finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


# running scaffolder with poa
echo "[$name] extending contigs using POA consensus method"
echo "..."
{ time ./release/$name -p $1 $2 $poa_file $poa_ext_file; } 2>&1 | awk '{
    printf "[scaffolder] extension finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


# running scaffolder with graphmap
echo "[$name] extending contigs using GraphMap"
echo "..."
{ time ./release/$name -g $1 $2 $gm_file $gm_ext_file; } 2>&1 | awk '{
    printf "[scaffolder] extension finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


echo "[BWA] indexing $3"
echo "..."
bwa_index="bwa index $3"
# Keep stdout unmolested
exec 3>&1
{ time $bwa_index 1>&3; } 2>&1 | awk '{
    printf "[BWA] indexing finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
exec 3>&-
echo ""


echo "[BWA] aligning global realign extended contigs to reference genome"
echo "..."
bwa_mem="bwa mem -t $num_threads -x pacbio $3 $gr_ext_file"
{ time $bwa_mem > $gr_aln_file; } 2>&1 | awk '{
    printf "[BWA] alignment finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


echo "[BWA] aligning POA extended contigs to reference genome"
echo "..."
bwa_mem="bwa mem -t $num_threads -x pacbio $3 $poa_ext_file"
{ time $bwa_mem > $poa_aln_file; } 2>&1 | awk '{
    printf "[BWA] alignment finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


echo "[BWA] aligning GraphMap extended contigs to reference genome"
echo "..."
bwa_mem="bwa mem -t $num_threads -x pacbio $3 $gm_ext_file"
{ time $bwa_mem > $gm_aln_file; } 2>&1 | awk '{
    printf "[BWA] alignment finished in %d hours, %d minutes and %.3f seconds\n",
           $1/3600, $1%3600/60, $1%60
}' | tail -1
echo ""


echo "[ANALYSIS] analyzing .sam files"
python3 scripts/extension_analysis.py $3 $poa_aln_file $gr_aln_file $gm_aln_file
echo "[ANALYSIS] analysis finshed"

rm -rf "./tmp"
