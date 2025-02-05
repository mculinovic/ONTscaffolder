/**
 * @file scaffolder.cpp
 * @copyright Marko Culinovic <marko.culinovic@gmail.com>
 * @copyright Luka Sterbic <luka.sterbic@gmail.com>
 * @brief Implementation file for scaffolder namespace.
 * @details Implementation file for scaffolder namespace. It provides
 * functions for different methods of contig extension.
 */
#include <seqan/sequence.h>
#include <cpppoa/poa.hpp>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>
#include <utility>
#include <memory>
#include <unordered_map>

#include "aligners/aligner.h"
#include "utility.h"
#include "scaffolder.h"
#include "extension.h"
#include "bases.h"


#define INNER_MARGIN 5  // margin for soft clipping port on read ends
#define OUTER_MARGIN 15
#define MIN_COVERAGE 5  // minimum coverage for position


using std::vector;
using std::string;
using std::reverse;
using std::pair;
using std::shared_ptr;
using std::unordered_map;

using seqan::CharString;
using seqan::Dna5String;
using seqan::StringSet;
using seqan::BamAlignmentRecord;
using seqan::toCString;
using seqan::String;
using seqan::CStyle;
using seqan::length;

using bases::BasesCounter;


namespace scaffolder {


int max_ext_length = 1000;
int inner_margin = 5;
int outer_margin = 15;
int min_coverage = 5;


const char *tmp_contig_file = "tmp/extend_contig.fasta";
const char *tmp_reads_file = "tmp/realign_reads.fasta";
const char *tmp_sam_file = "tmp/realign.sam";


void set_max_extension_len(int length) {
    if (length > 0) {
        max_ext_length = length;
    } else {
        utility::exit_with_message("Illegal extension length");
    }
}


void set_inner_margin(int margin) {
    if (margin >= 0) {
        inner_margin = margin;
    } else {
        utility::exit_with_message("Illegal inner margin");
    }
}


void set_outer_margin(int margin) {
    if (margin >= 0 && margin >= inner_margin) {
        outer_margin = margin;
    } else {
        utility::exit_with_message("Illegal outer margin");
    }
}


void set_min_coverage(int coverage) {
    if (coverage > 0) {
        min_coverage = coverage;
    } else {
        utility::exit_with_message("Illegal minimum coverage");
    }
}


void find_possible_extensions(const vector<BamAlignmentRecord>& aln_records,
                              vector<shared_ptr<Extension>>* pleft_ext_reads,
                              vector<shared_ptr<Extension>>* pright_ext_reads,
                              const unordered_map<string, uint32_t>&
                              read_name_to_id,
                              uint64_t contig_len) {
    auto& left_ext_reads = *pleft_ext_reads;
    auto& right_ext_reads = *pright_ext_reads;

    for (auto const& record : aln_records) {
        // get read name as cpp string
        String<char, CStyle> tmp_name = record.qName;
        string read_name(tmp_name);

        // record is suitable for extending contig
        // if it is soft clipped and clipped part extends
        // left of contig start
        // example:
        // contig ->     ------------
        // read ->  ----------
        if ((record.flag & UNMAPPED) == 0 &&
            record.cigar[0].operation == 'S' &&
            record.beginPos < OUTER_MARGIN &&
            record.cigar[0].count > (uint32_t) record.beginPos) {
            // length of extension
            int len = record.cigar[0].count - record.beginPos;
            String<char, CStyle> tmp = record.seq;
            string seq(tmp);

            uint32_t read_id = read_name_to_id.find(read_name)->second;

            if (record.beginPos < INNER_MARGIN) {
                int start = len - max_ext_length;
                if (len <= max_ext_length) {
                    start = 0;
                }

                string extension = seq.substr(start, max_ext_length);
                // reverse it because when searching for next base
                // in contig extension on left side we're moving
                // in direction right to left: <--------
                reverse(extension.begin(), extension.end());

                shared_ptr<Extension> ext(new Extension(
                    read_id, extension, false));
                left_ext_reads.emplace_back(ext);
            } else {
                shared_ptr<Extension> ext(new Extension(
                    read_id, string(), true));
                left_ext_reads.emplace_back(ext);
            }
        }

        int cigar_len = length(record.cigar);

        // if it is soft clipped and clipped part extends
        // right of contig start
        // example:
        // contig ->  ------------
        // read ->            ----------
        if ((record.flag & UNMAPPED) == 0 &&
            record.cigar[cigar_len - 1].operation == 'S') {
            // iterate over cigar string to get lengths of
            // read and contig parts used in alignment
            int used_read_size = 0;
            int used_contig_size = 0;

            for (auto const& e : record.cigar) {
                if (utility::contributes_to_seq_len(e.operation)) {
                    used_read_size += e.count;
                }
                if (utility::contributes_to_contig_len(e.operation)) {
                    used_contig_size += e.count;
                }
            }

            int right_clipping_len = record.cigar[cigar_len - 1].count;
            used_read_size -= right_clipping_len;
            int len = right_clipping_len -
                      (contig_len - (record.beginPos + used_contig_size));
            int margin = contig_len - (record.beginPos + used_contig_size);

            // if alignment ends more than 10 bases apart from contig
            // end skip read
            if (margin > OUTER_MARGIN) {
                continue;
            }

            // if read doesn't extend right of contig skip it
            if (len <= 0) {
                continue;
            }

            String<char, CStyle> tmp = record.seq;
            string seq(tmp);

            string extension = seq.substr(
                used_read_size + (right_clipping_len - len),
                max_ext_length);

            uint32_t read_id = read_name_to_id.find(read_name)->second;
            bool drop = margin > INNER_MARGIN;
            shared_ptr<Extension> ext(new Extension(read_id,
                drop ? string() : extension, drop));
             right_ext_reads.emplace_back(ext);
        }
    }
}


string get_extension_mv_simple(
    const vector<shared_ptr<Extension>>& extensions) {
    // calculate extension by majority vote
    string extension("");

    for (uint32_t i = 0; true; ++i) {
        BasesCounter bases = bases::count_bases(extensions);

        if (bases.coverage >= MIN_COVERAGE) {
            char output_base = utility::idx_to_base(bases.max_idx);
            extension.push_back(output_base);

            DEBUG_BLOCK(
                std::cerr << i << "\t" << output_base << "\t";
                for (int i = 0; i < NUM_BASES; ++i) {
                    std::cerr << bases.count[i] << "\t";
                }
                std::cerr << std::endl;
            )
        } else {
            // break when coverage below minimum
            break;
        }
    }

    return extension;
}


string get_extension_mv_realign(
    const vector<shared_ptr<Extension>>& extensions) {
    string contig_ext("");

    for (uint32_t i = 0; true; ++i) {
        BasesCounter bases = bases::count_bases(extensions);

        if (bases.coverage >= MIN_COVERAGE) {
            char output_base = utility::idx_to_base(bases.max_idx);

            // test output
            DEBUG_BLOCK(
                std::cerr << i << "\t" << output_base << "\t";
                for (int i = 0; i < NUM_BASES; ++i) {
                    std::cerr << bases.count[i] << "\t";
                }
                std::cerr << std::endl;
            )

            // realignment
            auto is_read_eligible = [output_base](char c) -> bool {
                return c == output_base;
            };

            // majority vote for next base
            BasesCounter next_bases = bases::count_bases(extensions,
                                                         is_read_eligible,
                                                         1);

            char next_mv = utility::idx_to_base(next_bases.max_idx);

            if (next_bases.coverage < 0.6 * MIN_COVERAGE) {
                DEBUG_BLOCK(
                    std::cerr << "coverage: " << bases.coverage << std::endl;
                    std::cerr << "next_max_idx: " << next_bases.max_idx;
                    std::cerr << std::endl << "next coverage: ";
                    std::cerr << next_bases.coverage << std::endl;
                )
                break;
            }

            // output base only if confirmed by the next majority vote
            contig_ext.push_back(output_base);

            // cigar operation check
            for (size_t j = 0; j < extensions.size(); ++j) {
                auto& extension = extensions[j];
                auto& seq = extension->seq();

                // skip dropped reads
                if (extension->is_droped) {
                    continue;
                }

                // skip extensions which don't have at least 2 bases left
                if (extension->curr_pos() >= seq.length() - 2) {
                    extension->is_droped = true;
                    continue;
                }

                char current_base = seq[extension->curr_pos()];
                char next_base = seq[extension->curr_pos() + 1];

                if (current_base == output_base) {
                    // if operation is hit move forward
                    extension->do_operation(match);
                } else if (current_base == next_mv) {
                    // if operation is a deletion stay - do nothing
                    extension->do_operation(deletion_1);
                } else if (next_base == next_mv) {
                    // if operation is a mismatch move forward
                    extension->do_operation(mismatch);
                } else if (next_base == output_base) {
                    // if operation is an insertion skip one and
                    // move to the next one
                    extension->do_operation(insertion_1);
                } else {
                    // drop read
                    extension->is_droped = true;
                }
            }

        } else {
            // break when coverage below minimum
            DEBUG_VAR(bases.coverage);
            break;
        }
    }

    return contig_ext;
}


Contig* extend_contig(Dna5String& contig_seq,
                      const vector<BamAlignmentRecord>& aln_records,
                      const unordered_map<string, uint32_t>& read_name_to_id,
                      const StringSet<CharString>& read_ids,
                      const StringSet<Dna5String>& read_seqs) {
    vector<shared_ptr<Extension>> left_extensions;
    vector<shared_ptr<Extension>> right_extensions;

    find_possible_extensions(aln_records,
                         &left_extensions,
                         &right_extensions,
                         read_name_to_id,
                         length(contig_seq));

    bool should_ext_left = true;
    bool should_ext_right = true;

    int total_left_ext = 0;
    int total_right_ext = 0;

    DEBUG("Total start: " << length(contig_seq))

    while (should_ext_left || should_ext_right) {
        string left_extension;
        string right_extension;

        // do left extension if needed
        if (should_ext_left) {
            DEBUG("Left extension:")

            left_extension = get_extension_mv_realign(left_extensions);
            reverse(left_extension.begin(), left_extension.end());
            should_ext_left = !left_extension.empty();

            total_left_ext += left_extension.length();
        }

        // do right extension if needed
        if (should_ext_right) {
            DEBUG("Right extension:")

            right_extension = get_extension_mv_realign(right_extensions);

            should_ext_right = !right_extension.empty();

            total_right_ext += right_extension.length();
        }

        should_ext_left = should_ext_left && total_left_ext < max_ext_length;
        should_ext_right = should_ext_right && total_right_ext < max_ext_length;

        DEBUG_BLOCK(
            std::cerr << "TR: " << total_right_ext << " " << right_extension;
            std::cerr << std::endl << "SER: " << should_ext_right << ", SEL: ";
            std::cerr << should_ext_left << std::endl;
        )

        // construct extended contig sequence
        Dna5String tmp_contig_seq = left_extension;
        tmp_contig_seq += contig_seq;
        tmp_contig_seq += right_extension;
        contig_seq = tmp_contig_seq;

        // prepare structure for realignment
        utility::write_fasta("contig", contig_seq, tmp_contig_file);

        StringSet<CharString> dropped_read_ids;
        StringSet<Dna5String> dropped_read_seqs;

        vector<shared_ptr<Extension>> tmp_left_extensions;
        vector<shared_ptr<Extension>> tmp_right_extensions;

        vector<bool> realign_reads(length(read_ids), false);
        bool will_realign = false;

        // check which left extending reads need to be realigned
        for (auto& ext : left_extensions) {
            if (ext->is_droped) {
                int read_id = ext->read_id();

                if (!realign_reads[read_id]) {
                    realign_reads[read_id] = true;
                    appendValue(dropped_read_ids, read_ids[read_id]);
                    appendValue(dropped_read_seqs, read_seqs[read_id]);
                    will_realign = true;
                }
            } else {
                tmp_left_extensions.emplace_back(ext);
            }
        }

        // check which right extending reads need to be realigned
        for (auto& ext : right_extensions) {
            if (ext->is_droped) {
                int read_id = ext->read_id();

                if (!realign_reads[read_id]) {
                    realign_reads[read_id] = true;
                    appendValue(dropped_read_ids, read_ids[read_id]);
                    appendValue(dropped_read_seqs, read_seqs[read_id]);
                    will_realign = true;
                }
            } else {
                tmp_right_extensions.emplace_back(ext);
            }
        }

        // if nothing needs realignment return the current extension
        if (!will_realign) {
            break;
        }

        // prepare the extension vectors for the next iteration
        left_extensions.clear();
        right_extensions.clear();

        left_extensions = std::move(tmp_left_extensions);
        right_extensions = std::move(tmp_right_extensions);

        utility::write_fasta(dropped_read_ids, dropped_read_seqs,
                             tmp_reads_file);

        // run aligner
        Aligner::get_instance().index(tmp_contig_file);

        Aligner::get_instance().align(tmp_contig_file, tmp_reads_file,
                                      tmp_sam_file, true);

        // load new alignments
        BamHeader header;
        vector<BamAlignmentRecord> records;
        utility::read_sam(&header, &records, tmp_sam_file);

        // find the extensions for the next iteration
        find_possible_extensions(records,
                                 &left_extensions,
                                 &right_extensions,
                                 read_name_to_id,
                                 length(contig_seq));

        // if the size of the left and the right extension are both below the
        // minimum coverage return the current contig extension
        if (left_extensions.size() < MIN_COVERAGE &&
            right_extensions.size() < MIN_COVERAGE) {
            break;
        }
    }

    return new Contig(contig_seq, total_left_ext, total_right_ext);
}

Contig* extend_contig_poa(const Dna5String& contig_seq,
                    const vector<BamAlignmentRecord>& aln_records,
                    const unordered_map<string, uint32_t>& read_name_to_id) {
    vector<shared_ptr<Extension>> left_extensions;
    vector<shared_ptr<Extension>> right_extensions;

    find_possible_extensions(aln_records,
                         &left_extensions,
                         &right_extensions,
                         read_name_to_id,
                         length(contig_seq));

    vector<string> extensions;
    for (auto& ext : left_extensions) {
        if (!ext->seq().empty()) {
            extensions.emplace_back(ext->seq().substr(0, max_ext_length));
        }
    }

    string left_extension = poa_consensus(extensions);
    reverse(left_extension.begin(), left_extension.end());

    extensions.clear();
    for (auto &ext : right_extensions) {
        if (!ext->seq().empty()) {
            extensions.emplace_back(ext->seq().substr(0, max_ext_length));
        }
    }

    string right_extension = poa_consensus(extensions);

    return new Contig(contig_seq, left_extension, right_extension);
}

}  // namespace scaffolder
