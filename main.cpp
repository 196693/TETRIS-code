#include <cstdlib>
#include <chrono>
#include <vector>
#include <math.h>

#include "include/GraphIO.h"
#include "include/Graph.h"
#include "include/Digraph.h"
#include "include/GetAllCounts.h"
#include "include/TriangleEstimators.h"
#include  "include/Triadic.h"
#include "include/EstimatorUtil.h"
// #include "include/BaselineEstimators.h"
#include "include/util/ConfigReader.h"
#include "include/EstimateEdgeCount.h"

#include "include/baseline/VertexMCMC.h"
#include "include/baseline/SubgraohRandomWalk_SRW.h"
#include "include/baseline/SEC.h"
#include "include/baseline/SERWC.h"
#include "include/baseline/UESS.h"
#include "include/TETRIS.h"

using namespace Escape;

/**
 *Usage: ./SubgraphCount script_file
 * For a sample script file and its description, see the config/flickr-baseline.txt
 */


int main(int argc, char *argv[]) {

    CGraph cg;
    if (argc != 2) {
        std::cout << "Usage File: ./SubgraphCount script_file\n\n";
        return 0;
    }
    config_params cfp = LoadConfig(argv[1]);

    for (int i = 0; i < cfp.input_files.size(); i++) {
        /**
         * Load the input graph and print relevant parameters
         */
        auto startTime = std::chrono::high_resolution_clock::now();
        if (loadGraphCSR(cfp.input_files[i].c_str(), cg, 1))
            exit(1);
        auto endTime = std::chrono::high_resolution_clock::now();
        std::cout << "Time to load graph = " <<
                  std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count()
                  << " seconds" << std::endl;
        std::cout << "#Vertices = " << cg.nVertices << ",#Edges = " << cg.nEdges << std::endl;

        /**
         * Load relevant parameters
         */
        Parameters params;
        params.filename = cfp.input_files[i];
        params.no_of_repeat = cfp.no_of_repeats;
        params.print_to_console = cfp.print_to_console;
        params.print_to_file = cfp.print_to_file;
        params.normalization_count_available = cfp.edge_count_available;

        /**
         * Set up randomness initiator
         */
        std::random_device rd;
        std::mt19937 mt(rd());

        /**
         * For each sprisification parameter and seed count and algo_name,
         * run an instance
         */
        for (auto algo_name : cfp.algo_names) {
            for (auto sparsification_prob : cfp.sparsification_prob) {
                for (auto seed_count: cfp.seed_count) {
                    /**
                     * Update parameters for this particular run
                     */
                    params.algo_name = algo_name;
                    params.sparsification_prob = sparsification_prob;
                    params.walk_length = cg.nEdges * sparsification_prob; // g.nEdges is twice the number of edges
                    params.subsample_size = params.walk_length * cfp.subsample_prob;
                    EdgeIdx triangle_count = cfp.triangle_count[i];
                    params.seed_count = seed_count;
                    params.seed_vertices.clear();

                    /**
                     * We fix the seed vertices and run for params.no_of_repeat many iterations with the
                     * seed vertex remaining fixed.
                     * If degree_bin_seed is false, then we just need to
                     * sample uniform random seed vertex from the entire graph.
                     * THIS IS THE NORMAL MODE IN WHICH ALL BASELINE AND OUR
                     * ALGORITHMS ARE EXECUTED
                     */
                    if (! cfp.degree_bin_seed) {
                        VertexIdx n = cg.nVertices;
                        std::uniform_int_distribution<VertexIdx> dist_seed_vertex(0, n - 1);
                        for (VertexIdx sC = 0; sC < params.seed_count; sC++) {
                            VertexIdx seed = dist_seed_vertex(mt); // TODO: verify randomness
                            params.seed_vertices.emplace_back(seed);
                        }
                        // Our Algorithm
                        if (algo_name == "TETRIS"){
                            params.algo_name = "_new_" + algo_name;
                            TriangleEstimator(&cg, params, triangle_count, TETRIS);
                        }
                           // Baseline: sample an edge and count the number of triangles incident on it. Then scale.
                    //    else if (algo_name == "EstTriByEdgeSampleAndCount")
                    //        TriangleEstimator(&cg, params, triangle_count, EstTriByEdgeSampleAndCount);
                    //        // Baseline:  do a random walk and count the number of triangles incident on each edge. Then scale.
                       else if (algo_name == "SERWC")
                           TriangleEstimator(&cg, params, triangle_count, SERWC);
                    //        // Baseline: do a random walk and count the teiangles in induces multi-graph. Scale.
                    //    else if (algo_name == "EstTriByRW")
                    //        TriangleEstimator(&cg, params, triangle_count, EstTriByRW);
                    //        // Sample an edge, sample a neighbor and estimate the teiangles incident on the neighbor. Scale up.
                    //    else if (algo_name == "EstTriByRWandNborSampling")
                    //        TriangleEstimator(&cg, params, triangle_count, EstTriByRWandNborSampling);
                    //        // Sample each edge with probability p and count traingles in the subsampled graph. Scale by 1/p^3.
                    //    else if (algo_name == "EstTriBySparsification")
                    //        TriangleEstimator(&cg, params, triangle_count, EstTriBySparsification);
                           // Uniformly Sample edges, and count the number of triangles in the multi-graph.
                       else if (algo_name == "UESS")
                           TriangleEstimator(&cg, params, triangle_count, UESS);
                        // else if (algo_name == "EdgeEstimator")
                        //     EdgeEstimator(&cg, params);
                        // else if (algo_name == "DegreeSqEstimator")
                        //             DegreeSqEstimator(&cg, params);
                       else if (algo_name == "VertexMCMC")
                           TriangleEstimator(&cg, params, triangle_count, VertexMCMC);
                       else if (algo_name == "SRW1")
                           TriangleEstimator(&cg, params, triangle_count, SRW1);
                        else
                            std::cout << "Unknown algorithm option. \n";
                    }
                        // Otherwise, we iterate over all the vertices, and sample 5 vertex from each degree range
                        // to figure out the degree range, we take the log of the degree.
                        // WE ONLY NEED TO DO THIS FOR OUR ALGORITHM.
                    else {
                        params.seed_vertices.emplace_back(-1); // dummy place holder for now
                        VertexIdx num_of_bucket = floor(log10(cg.nVertices));
                        std::vector<std::vector<VertexIdx >> deg_bins(num_of_bucket);
                        std::vector<std::vector<VertexIdx >> random_seeds(num_of_bucket);
                        for (VertexIdx v = 0; v < cg.nVertices; v++) {
                            VertexIdx deg = cg.degree(v);
                            if (deg != 0) {
                                VertexIdx bucket_id = floor(log10(deg));
                                deg_bins[bucket_id].emplace_back(v);
                            }
                        }
                        // Now sample 5 vertices uniformly at random from each bin (with replacement)
                        for (int nb = 0; nb < num_of_bucket; nb++) {
                            if (!deg_bins[nb].empty()) {
                                std::uniform_int_distribution<VertexIdx> dist_bucket_seed_vertex(0, deg_bins[nb].size() - 1);
                                for (int j = 0; j < 5; j++) {
                                    VertexIdx random_seed = dist_bucket_seed_vertex(mt);
                                    random_seeds[nb].emplace_back(deg_bins[nb][random_seed]);
                                }
                            }
                        }
                        // Clear the excess memory created by this seeding process.
                        std::vector<std::vector<VertexIdx >>().swap(deg_bins);
                        if (algo_name == "TETRIS"){
                            for (int nb = 0; nb < num_of_bucket; nb++) {
                                if (!random_seeds[nb].empty()) {
                                    for (int j = 0; j < 4; j++) {
                                        VertexIdx seed = random_seeds[nb][j];
                                        params.seed_vertices[0]= seed;
                                        params.algo_name = "EstTriByRWandWghtedSampling_" + std::to_string(nb);
                                        TriangleEstimator(&cg, params, triangle_count, TETRIS);
                                    }
                                }
                            }
                        }
                        else
                            std::cout << "Unknown algorithm option. \n";
                    }
                }
            }

        }
    }
    return 0;
}