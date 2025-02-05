/**
 * @file aligner.h
 * @copyright Marko Culinovic <marko.culinovic@gmail.com>
 * @copyright Luka Sterbic <luka.sterbic@gmail.com>
 * @brief Declaration of an abstract aligner class.
 * @details Contains the minimum interface that an aligner needs to implement in
 * order to be usable by eagler.
 */

#ifndef ALIGNERS_ALIGNER_H
#define ALIGNERS_ALIGNER_H

#include <seqan/sequence.h>
#include <string>


using seqan::CharString;
using seqan::Dna5String;


namespace read_type {

/**
 * @brief Enum used to distinguish read types.
 */
enum ReadType {
    PacBio,
    ONT
};


/**
 * @brief Converts the given string to a ReadType enumerator object
 *
 * @param read_type C-style string ID of the read type
 * @return the enum associated to the passed string
 */
ReadType string_to_read_type(const char *read_type);

}  // namespace read_type


/**
 * @brief Class representing an abstract aligner.
 * @details The Aligner class is an abstract class that defines the minimum
 * interface that an aligner needs to implment in order to be used in the
 * scaffolding pipeline.
 */
class Aligner {
 private:
    /**
     * @brief Path to a SAM file for temporary usage.
     */
    static const char *tmp_alignment_filename;

    /**
     * @brief Path to a FASTA file to temporarly store a reference/draft genome.
     */
    static const char *tmp_reference_filename;

    /**
     * @brief Path to a FASTA file to temporarly store a single contig.
     */
    static const char *tmp_contig_filename;

    /**
     * @brief The shared instance of a class implementing the Aligner interface
     * that will be used by all the components of the scaffolding pipeline.
     */
    static Aligner *instance;

 protected:
    /**
     * @brief The name of the aligner.
     */
    std::string name;

    /**
     * @brief The type of reads that will be used as input.
     */
    read_type::ReadType tech_type;

    /**
     * @brief Constructor for Aligner
     *
     * @param name the name of the aligner
     * @param tech_type the type of the reads that will be used as input
     */
    Aligner(const std::string& name, read_type::ReadType tech_type)
        : name(name), tech_type(tech_type) {}

 public:
    /**
     * @brief The default virtual destructor for this class.
     */
    virtual ~Aligner() = default;

    /**
     * @brief Generate an index for the given genome.
     * @details Creates the index that is used during alignment for the given
     * genome. The output files are defined by the invoked external command.
     *
     * @param filename path to a genome in FASTA format
     */
    virtual void index(const char* filename) = 0;

    /**
     * @brief Align the given reads to a reference genome.
     * @param reference_file [description]
     * @param reads_file     [description]
     */
    virtual void align(const char* reference_file,
                       const char* reads_file) = 0;

    virtual void align(const char* reference_file,
                       const char* reads_file,
                       const char* sam_file,
                       bool only_primary) = 0;

    virtual void align(const char* reference_file,
                       const char* reads_file,
                       const char* sam_file) = 0;

    virtual void align(const CharString& id,
                       const Dna5String& contig,
                       const char* reads_filename) = 0;

    static const char *get_tmp_alignment_filename();
    static const char *get_tmp_reference_filename();
    static const char *get_tmp_contig_filename();

    static void init(bool use_graphmap_aligner, read_type::ReadType read_type);
    static Aligner& get_instance();

    const std::string& get_name() const;
};

#endif  // ALIGNERS_ALIGNER_H
