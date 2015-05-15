/**
 * TODO
 */

#ifndef UTILITY_H
#define UTILITY_H

#include <seqan/bam_io.h>
#include <vector>
#include <string>
#include <unordered_map>

using std::vector;
using std::string;
using std::invalid_argument;
using std::unordered_map;

using seqan::StringSet;
using seqan::CharString;
using seqan::Dna5String;
using seqan::BamHeader;
using seqan::BamAlignmentRecord;

/**
 * SAM format unmapped read flag
 */
#define UNMAPPED 0x4

/**
 * The size of the shell command buffer in bytes
 */
#define COMMAND_BUFFER_SIZE 160


/**
 * Structure used to cluster read alignments to specific contigs.
 * Key: the contig id
 * Value: a vector of alignments to that specific contig
 */
typedef unordered_map<int, vector<BamAlignmentRecord>> alignment_collection;



namespace utility {


/**
 * @brief The number of concurrent threads supported by the implementation
 */
extern const unsigned int hardware_concurrency;


/**
 * @brief Buffer to hold shell command strings
 */
extern char command_buffer[COMMAND_BUFFER_SIZE];


/**
 * @brief Reads a FASTA file
 * @details Reads sequences data from FASTA file and stores it in two sets:
 * sequences ids and sequences
 *
 * @param pids pointer to the set of ids
 * @param pseqs pointer to the set of sequences
 * @param ont_reads_filename [description]
 */
void read_fasta(StringSet<CharString>* pids,
                StringSet<Dna5String>* pseqs,
                char *ont_reads_filename);


/**
 * @brief Write a sequence to file
 * @details Writes a single sequence to a FASTA file.
 *
 * @param id string ID of the sequence
 * @param seq bases of the sequence
 * @param filename path to the output file
 */
void write_fasta(const CharString &id, const Dna5String &seq,
                 const char* filename);


/**
 * @brief Writes a set of sequences to file
 * @details Writes multiple sequences to a FASTA file. String ids and sequence
 * contents at the same index will be grouped together to form one FASTA entry.
 *
 * @param ids collection of the string ids of the sequences
 * @param seqs collection of the bases of the sequences
 * @param filename path to the output file
 */
void write_fasta(const StringSet<CharString>& ids,
                const StringSet<Dna5String>& seqs,
                const char *filename);


/**
 * @brief Read a SAM file
 * @details Reads alignment data from the given SAM file and stores the sequence
 * data in the objects passed as arguments to this function.
 *
 * @param pheader pointer to a BAM/SAM file header
 * @param precords pointer to the vector where alignments will be stored
 * @param filename path to the input SAM file
 */
void read_sam(BamHeader* pheader, vector<BamAlignmentRecord>* precords,
              const char* filename);

/**
 * @brief Reads and map aligned reads to contigs
 * @details Reads the given SAM file and clusters the alignments around the
 * contig they reference.
 *
 * @param filename path to SAM file with alignments
 * @param collection object to be filled by the function call
 */
void map_alignments(const char *filename,
                    alignment_collection *collection);


/**
 * @brief Run shell command
 * @details Executes a shell command given as a combination of a format string
 * and variable number of arguments to be inserted in the format string.
 *
 * @param format the format of the command
 * @param ... printf style arguments to fill the format string
 *
 * @throw std::runtime_error when the exit value of the command is not 0
 */
void execute_command(const char *format, ...);


/**
 * @brief Char base to int id
 * @details Converts a nucleotide character to its corresponding integer id. the
 * mapping used is {'A': 0, 'T': 1, 'G': 2, 'C': 3}.
 *
 * @param base nucleotide base
 *
 * @return integer base id
 * @throw std::invalid_argument when the base is not a valid nucleotide
 */
int base_to_idx(char base);


/**
 * @brief Base id to char base
 * @details Converts the given id to the correspondent nucleotide. The mapping
 * used is {0: 'A', 1: 'T', 2: 'G', 3: 'C'}.
 *
 * @param idx integer base id
 *
 * @return nucleotide character
 * @throw std::invalid_argument when idx < 0 or idx > 3
 */
char idx_to_base(int idx);

}  // namespace utility

#endif  // UTILITY_H
