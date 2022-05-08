/******************************************************************************
 * File:             RcppIRV.cpp
 *
 * Author:           Floyd Everest <me@floydeverest.com>
 * Created:          03/01/22
 * Description:      This file implements an Rcpp interface for the IRV
 *                   Dirichlet Tree methods, and for the IRV social choice
 *function.
 *****************************************************************************/

// [[Rcpp::plugins("cpp17")]]
// [[Rcpp::depends(RcppThread)]]

#include "dirichlet_tree.hpp"
#include "irv_ballot.hpp"
#include "irv_node.hpp"

#include <Rcpp.h>
#include <RcppThread.h>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

/*! \brief The IRV social choice function.
 *
 *  This function calculates an election outcome using the standard IRV social
 * choice function.
 *
 * \param  bs An Rcpp::List of ballots in CharacterVector representation.
 *
 * \param nWinners An integer indicating the number of winners to elect.
 *
 * \param seed A seed for the PRNG for tie-breaking.
 *
 * \return The winning candidate.
 */
// [[Rcpp::export]]
Rcpp::List RSocialChoiceIRV(Rcpp::List bs, unsigned nWinners,
                            std::string seed) {

  Rcpp::List out{};

  std::list<IRVBallotCount> scInput{};

  std::unordered_map<std::string, size_t> c2Index{};
  std::vector<std::string> cNames{};

  Rcpp::CharacterVector bNames;
  std::string cName;
  std::list<unsigned> bIndices;

  for (auto i = 0; i < bs.size(); ++i) {
    bNames = bs[i];
    if (bNames.size() == 0) // Skip empty ballots
      continue;
    bIndices = {};
    for (auto j = 0; j < bNames.size(); ++j) {
      cName = bNames[j];
      // If candidate has not yet been seen, add it to our map and vector.
      if (c2Index.count(cName) == 0) {
        c2Index[cName] = cNames.size();
        cNames.push_back(cName);
      }
      bIndices.push_back(c2Index[cName]);
    }
    scInput.emplace_back(std::move(bIndices), 1);
  }

  if (nWinners < 1 || nWinners >= cNames.size())
    Rcpp::stop("`nWinners` must be >= 1 and <= the number of candidates.");

  if (scInput.size() == 0)
    Rcpp::stop("No valid ballots for the IRV social choice function.");

  // Seed the PRNG.
  std::seed_seq ss(seed.begin(), seed.end());
  std::mt19937 e(ss);
  e.discard(e.state_size * 100);

  std::vector<unsigned> elimination_order_idx =
      socialChoiceIRV(scInput, cNames.size(), &e);

  Rcpp::CharacterVector elimination_order{};
  Rcpp::CharacterVector winners{};

  for (auto i = 0; i < cNames.size() - nWinners; ++i) {
    elimination_order.push_back(cNames[elimination_order_idx[i]]);
  }
  for (auto i = cNames.size() - nWinners; i < cNames.size(); ++i) {
    winners.push_back(cNames[elimination_order_idx[i]]);
  }

  out("elimination_order") = elimination_order;
  out("winners") = winners;

  return out;
}

/*! \brief An Rcpp object which implements the `dtree` R object interface.
 *
 *  This class exposes all interfaces for the partially-ordered, IRV-ballot
 * Dirichlet Tree.
 */
class PIRVDirichletTree {
private:
  // The underlying Dirichlet Tree.
  DirichletTree<IRVNode, IRVBallot, IRVParameters> *tree;

  // A vector of candidate names.
  Rcpp::CharacterVector candidateVector{};

  // A map of candidate names to their ballot index.
  std::unordered_map<std::string, size_t> candidateMap{};

  // A record of the number of observed ballots.
  size_t nObserved = 0;

  /*! \brief Converts an R list of valid IRV ballot vectors to a
   * std::list<IRVBallotCount> format.
   *
   *  In R, we consider a matrix of ballots to be that with columns
   * corresponding to each preference choice, and elements corresponding to the
   * index of the candidate.
   *
   * \param bs An Rcpp::List of ballots (assumed to be in Rcpp::CharacterVector
   * representation).
   *
   * \return A list of IRVBallotCount objects.
   */
  std::list<IRVBallotCount> parseBallotList(Rcpp::List bs) {
    Rcpp::CharacterVector namePrefs;
    std::string cName;
    std::list<unsigned> indexPrefs;
    size_t cIndex;

    std::list<IRVBallotCount> out;

    // We iterate over each ballot, and convert it into an IRVBallotCount using
    // the "candidate index" for each seen candidate.
    for (auto i = 0; i < bs.size(); ++i) {
      namePrefs = bs[i];
      indexPrefs = {};
      for (auto j = 0; j < namePrefs.size(); ++j) {
        cName = namePrefs[j];

        // Find index for the candidate. Add it to our set if it doesn't exist.
        if (candidateMap.count(cName) == 0) {
          Rcpp::stop("Unknown candidate encountered in ballot!");
        } else {
          cIndex = candidateMap[cName];
        }

        indexPrefs.push_back(cIndex);
      }
      out.emplace_back(std::move(indexPrefs), 1);
    }

    return out;
  }

public:
  // Constructor/destructor
  PIRVDirichletTree(Rcpp::CharacterVector candidates, unsigned minDepth_,
                    float alpha0_, std::string seed_) {
    // Parse the candidate strings.
    std::string cName;
    size_t cIndex = 0;
    for (auto i = 0; i < candidates.size(); ++i) {
      cName = candidates[i];
      candidateVector.push_back(cName);
      candidateMap[cName] = cIndex;
      ++cIndex;
    }
    // Initialize tree.
    IRVParameters params(candidates.size(), minDepth_, alpha0_);
    tree = new DirichletTree<IRVNode, IRVBallot, IRVParameters>(params, seed_);
  }
  ~PIRVDirichletTree() { delete tree; }

  // Getters
  unsigned getNCandidates() { return tree->getParameters()->getNCandidates(); }
  unsigned getMinDepth() { return tree->getParameters()->getMinDepth(); }
  float getAlpha0() { return tree->getParameters()->getAlpha0(); }

  // Setters
  void setMinDepth(unsigned minDepth_) {
    tree->getParameters()->setMinDepth(minDepth_);
  }
  void setAlpha0(float alpha0_) { tree->getParameters()->setAlpha0(alpha0_); }
  void setSeed(std::string seed_) { tree->setSeed(seed_); }

  // Other methods
  void reset() {
    tree->reset();
    nObserved = 0;
  }

  void update(Rcpp::List ballots) {
    std::list<IRVBallotCount> bcs = parseBallotList(ballots);
    for (IRVBallotCount &bc : bcs) {
      nObserved += bc.second;
      tree->update(bc);
    }
  }

  Rcpp::List samplePredictive(unsigned nSamples, std::string seed) {

    tree->setSeed(seed);

    Rcpp::List out;
    Rcpp::CharacterVector rBallot;

    std::list<IRVBallotCount> samples = tree->sample(nSamples);
    for (auto &[b, count] : samples) {
      // Push count * b to the list.
      for (auto i = 0; i < count; ++i) {
        rBallot = Rcpp::CharacterVector::create();
        for (auto cIndex : b.preferences) {
          rBallot.push_back(candidateVector[cIndex]);
        }
        out.push_back(rBallot);
      }
    }

    return out;
  }

  Rcpp::NumericVector samplePosterior(unsigned nElections, unsigned nBallots,
                                      unsigned nWinners, unsigned nBatches,
                                      std::string seed) {

    if (nBallots < nObserved)
      Rcpp::stop("`nBallots` must be larger than the number of ballots "
                 "observed to obtain the posterior.");

    tree->setSeed(seed);

    size_t nCandidates = getNCandidates();

    // Generate nBatches PRNGs.
    std::mt19937 *treeGen = tree->getEnginePtr();
    std::vector<unsigned> seeds{};
    for (auto i = 0; i <= nBatches; ++i) {
      seeds.push_back((*treeGen)());
    }
    // TODO: Remove this?
    treeGen->discard(treeGen->state_size * 100);

    // The number of elections to sample per thread.
    unsigned batchSize, batchRemainder;
    if (nElections <= 1) {
      batchSize = 0;
      batchRemainder = nElections;
    } else {
      batchSize = nElections / nBatches;
      batchRemainder = nElections % nBatches;
    }

    // The results vector for each thread.
    std::vector<std::vector<std::vector<unsigned>>> results(nBatches + 1);

    // Use RcppThreads to compute the posterior in batches.
    auto getBatchResult = [&](size_t i, size_t batchSize) -> void {
      // Check for interrupt.
      RcppThread::checkUserInterrupt();

      // Seed a new PRNG, and warm it up.
      std::mt19937 e(seeds[i]);
      e.discard(e.state_size * 100);

      // Simulate elections.
      std::list<std::list<IRVBallotCount>> elections =
          tree->posteriorSets(batchSize, nBallots, &e);

      for (auto &el : elections)
        results[i].push_back(socialChoiceIRV(el, nCandidates, &e));
    };

    // Dispatch the jobs.
    RcppThread::ThreadPool pool(std::thread::hardware_concurrency());

    // Process batches on workers
    pool.parallelFor(0, nBatches,
                     [&](size_t i) { getBatchResult(i, batchSize); });

    // Process remainder on main thread.
    if (batchRemainder > 0)
      getBatchResult(nBatches, batchRemainder);

    pool.join();

    // Aggregate the results
    Rcpp::NumericVector out(nCandidates);
    out.names() = candidateVector;

    for (auto j = 0; j <= nBatches; ++j) {
      for (auto elimination_order_idx : results[j]) {
        for (auto i = nCandidates - nWinners; i < nCandidates; ++i)
          out[elimination_order_idx[i]] = out[elimination_order_idx[i]] + 1;
      }
    }

    out = out / nElections;
    return out;
  }

  Rcpp::NumericVector sampleMarginalProbability(unsigned nSamples,
                                                Rcpp::CharacterVector ballot,
                                                std::string seed) {
    tree->setSeed(seed);

    float prob;
    Rcpp::NumericVector out = {};
    std::string name;

    std::list<unsigned> preferences = {};
    for (auto i = 0; i < ballot.size(); ++i) {
      name = ballot[i];
      preferences.push_back(candidateMap[name]);
    }

    IRVBallot b(preferences);

    for (auto i = 0; i < nSamples; ++i) {
      prob = tree->marginalProbability(b, nullptr);
      out.push_back(prob);
    }

    return out;
  }
};

// The Rcpp module interface.
RCPP_MODULE(pirv_dirichlet_tree_module) {
  Rcpp::class_<PIRVDirichletTree>("PIRVDirichletTree")
      // Constructor needs nCandidates, minDepth, alpha0 and seed.
      .constructor<Rcpp::CharacterVector, unsigned, float, std::string>()
      // Getter/Setter interface
      .property("nCandidates", &PIRVDirichletTree::getNCandidates)
      .property("alpha0", &PIRVDirichletTree::getAlpha0,
                &PIRVDirichletTree::setAlpha0)
      .property("minDepth", &PIRVDirichletTree::getMinDepth,
                &PIRVDirichletTree::setMinDepth)
      .method("setSeed", &PIRVDirichletTree::setSeed)
      // Methods
      .method("reset", &PIRVDirichletTree::reset)
      .method("update", &PIRVDirichletTree::update)
      .method("samplePredictive", &PIRVDirichletTree::samplePredictive)
      .method("samplePosterior", &PIRVDirichletTree::samplePosterior)
      .method("sampleMarginalProbability",
              &PIRVDirichletTree::sampleMarginalProbability);
}
