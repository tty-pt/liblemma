export declare function foldAccents(text: string, options?: { wasm?: boolean }): string;
export declare function tokenize(text: string, options?: { wasm?: boolean }): string[];
export declare function encodeTokens(text: string): Uint16Array;
export declare function decodeTokens(tokens: Uint16Array | ArrayLike<number>): string;

export declare class LemmaIndex {
  constructor(specOrMask?: string | number);
  addDocument(id: number, text: string, field?: string): boolean;
  removeDocument(id: number, field?: string): boolean;
  storeTyped(ref: number, uint8Array: Uint8Array, qtype?: number): boolean;
  fill(query: string, options?: { field?: string; phrase?: boolean }): number[];
  rank(queryOrMatchedCount: string | number, ref: number, field?: string): number;
  search(query: string, options?: { field?: string; phrase?: boolean; topK?: number }): Array<{ id: number; score: number }>;
  clear(): void;
  close(): void;
}

export declare class LemmaLexicalAxis {
  readonly name: string;
  constructor(specOrMask?: string | number);
  registerText(ref: number, text: string, field?: string): void;
  unstore(ref: number, field?: string): boolean;
  storeTyped(ref: number, uint8Array: Uint8Array, qtype?: number): boolean;
  fill(params: { term?: string; query?: string; field?: string; phrase?: boolean }): number[];
  rank(params: { term?: string; query?: string; field?: string }, ref: number): number;
  close(): void;
}
